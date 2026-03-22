#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "pico/time.h"

#include "lwip/dns.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"

#define BUTTON_PIN 14
#define POT_PIN 26
#define ADC_CHANNEL 0

#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""

#define API_HOST "interramal-marquita-heavenless.ngrok-free.dev"
#define API_PORT 80
#define API_PATH "/telemetry"

#define DEVICE_ID "pico-w-01"
#define QUEUE_SIZE 16

typedef struct {
  char device_id[32];
  char timestamp[40];
  char sensor_type[32];
  char reading_type[16];
  float value;
} telemetry_t;

typedef struct {
  telemetry_t items[QUEUE_SIZE];
  int head;
  int tail;
  int count;
} telemetry_queue_t;

volatile bool button_event = false;
volatile int last_button_state = 1;
volatile bool adc_read_flag = false;
static volatile uint64_t last_irq_us = 0;

telemetry_queue_t queue;

typedef enum {
  STATE_BOOT,
  STATE_WIFI_CONNECTING,
  STATE_WIFI_READY,
  STATE_SEND_PENDING,
  STATE_RETRY_WAIT
} app_state_t;

app_state_t state = STATE_BOOT;
uint64_t retry_until_us = 0;

// ====== FILA ======
void queue_init(telemetry_queue_t *q) {
  q->head = 0;
  q->tail = 0;
  q->count = 0;
}

bool queue_push(telemetry_queue_t *q, telemetry_t *item) {
  if (q->count >= QUEUE_SIZE) return false;
  q->items[q->tail] = *item;
  q->tail = (q->tail + 1) % QUEUE_SIZE;
  q->count++;
  return true;
}

bool queue_pop(telemetry_queue_t *q, telemetry_t *out) {
  if (q->count == 0) return false;
  *out = q->items[q->head];
  q->head = (q->head + 1) % QUEUE_SIZE;
  q->count--;
  return true;
}

bool queue_empty(telemetry_queue_t *q) {
  return q->count == 0;
}

// ====== SENSORES ======
void button_irq_handler(uint gpio, uint32_t events) {
  uint64_t now = time_us_64();

  if (now - last_irq_us < 200000) return; // debounce 200ms
  last_irq_us = now;

  last_button_state = gpio_get(BUTTON_PIN);
  button_event = true;
}

bool timer_callback(struct repeating_timer *t) {
  adc_read_flag = true;
  return true;
}

void init_button() {
  gpio_init(BUTTON_PIN);
  gpio_set_dir(BUTTON_PIN, GPIO_IN);
  gpio_pull_up(BUTTON_PIN);

  gpio_set_irq_enabled_with_callback(
    BUTTON_PIN,
    GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE,
    true,
    &button_irq_handler
  );
}

void init_adc() {
  adc_init();
  adc_gpio_init(POT_PIN);
  adc_select_input(ADC_CHANNEL);
}

float read_pot_percent() {
  const int samples = 8;
  uint32_t sum = 0;

  for (int i = 0; i < samples; i++) {
    sum += adc_read();
  }

  float avg = sum / (float)samples;
  return (avg / 4095.0f) * 100.0f;
}

// ====== TELEMETRIA ======
void build_timestamp(char *buffer, size_t size) {
  uint64_t sec = time_us_64() / 1000000ULL;
  int s = sec % 60;
  int m = (sec / 60) % 60;
  int h = (sec / 3600) % 24;

  snprintf(buffer, size, "2026-03-21T%02d:%02d:%02dZ", h, m, s);
}

void telemetry_to_json(telemetry_t *t, char *out, size_t size) {
  snprintf(
    out,
    size,
    "{"
    "\"device_id\":\"%s\","
    "\"timestamp\":\"%s\","
    "\"sensor_type\":\"%s\","
    "\"reading_type\":\"%s\","
    "\"value\":%.2f"
    "}",
    t->device_id,
    t->timestamp,
    t->sensor_type,
    t->reading_type,
    t->value
  );
}

void enqueue_button_telemetry() {
  telemetry_t item;
  memset(&item, 0, sizeof(item));

  strcpy(item.device_id, DEVICE_ID);
  build_timestamp(item.timestamp, sizeof(item.timestamp));
  strcpy(item.sensor_type, "button");
  strcpy(item.reading_type, "discrete");
  item.value = (last_button_state == 0) ? 1.0f : 0.0f;

  if (queue_push(&queue, &item)) {
    printf("[QUEUE] button enfileirado: %.0f\n", item.value);
  } else {
    printf("[QUEUE] fila cheia, button descartado\n");
  }
}

void enqueue_analog_telemetry() {
  telemetry_t item;
  memset(&item, 0, sizeof(item));

  strcpy(item.device_id, DEVICE_ID);
  build_timestamp(item.timestamp, sizeof(item.timestamp));
  strcpy(item.sensor_type, "potentiometer");
  strcpy(item.reading_type, "analog");
  item.value = read_pot_percent();

  if (queue_push(&queue, &item)) {
    printf("[QUEUE] analog enfileirado: %.2f\n", item.value);
  } else {
    printf("[QUEUE] fila cheia, analog descartado\n");
  }
}

// ====== HTTP ======
static bool request_done = false;
static bool request_ok = false;

static bool dns_done = false;
static bool dns_ok = false;
static ip_addr_t dns_addr;

typedef struct {
  char request[1024];
} http_context_t;

// ===== DNS CALLBACK =====
void dns_callback(const char *name, const ip_addr_t *ipaddr, void *arg) {
  if (ipaddr) {
    dns_addr = *ipaddr;
    dns_ok = true;
  } else {
    dns_ok = false;
  }
  dns_done = true;
}

// ===== TCP CALLBACK =====
static err_t tcp_connected(void *arg, struct tcp_pcb *tpcb, err_t err) {
  if (err != ERR_OK) {
    request_done = true;
    request_ok = false;
    return err;
  }

  http_context_t *ctx = (http_context_t *)arg;
  tcp_write(tpcb, ctx->request, strlen(ctx->request), TCP_WRITE_FLAG_COPY);
  tcp_output(tpcb);

  request_done = true;
  request_ok = true;
  return ERR_OK;
}

// ===== HTTP POST =====
bool http_post_json(const char *host, int port, const char *path, const char *json_body) {
  ip_addr_t addr;
  err_t err;

  dns_done = false;
  dns_ok = false;

  err = dns_gethostbyname(host, &addr, dns_callback, NULL);

  if (err == ERR_OK) {
    
  } else if (err == ERR_INPROGRESS) {
    printf("[DNS] resolvendo %s...\n", host);

    uint64_t start = time_us_64();
    while (!dns_done && (time_us_64() - start < 5000000ULL)) {
      cyw43_arch_poll();
      sleep_ms(10);
    }

    if (!dns_done || !dns_ok) {
      printf("[DNS] falha ao resolver host\n");
      return false;
    }

    addr = dns_addr;
  } else {
    printf("[DNS] erro imediato: %d\n", err);
    return false;
  }

  struct tcp_pcb *pcb = tcp_new_ip_type(IP_GET_TYPE(&addr));
  if (!pcb) {
    printf("[HTTP] falha ao criar TCP PCB\n");
    return false;
  }

  http_context_t ctx;
  snprintf(
    ctx.request,
    sizeof(ctx.request),
    "POST %s HTTP/1.1\r\n"
    "Host: %s\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: %d\r\n"
    "Connection: close\r\n"
    "\r\n"
    "%s",
    path,
    host,
    (int)strlen(json_body),
    json_body
  );

  request_done = false;
  request_ok = false;

  tcp_arg(pcb, &ctx);
  tcp_connect(pcb, &addr, port, tcp_connected);

  uint64_t start = time_us_64();
  while (!request_done && (time_us_64() - start < 5000000ULL)) {
    cyw43_arch_poll();
    sleep_ms(10);
  }

  tcp_close(pcb);
  return request_ok;
}

// ====== WIFI ======
bool wifi_connect() {
  printf("[WIFI] conectando em %s...\n", WIFI_SSID);

  int err = cyw43_arch_wifi_connect_timeout_ms(
    WIFI_SSID,
    WIFI_PASSWORD,
    CYW43_AUTH_OPEN,
    10000
  );

  if (err) {
    printf("[WIFI] falha na conexao: %d\n", err);
    return false;
  }

  printf("[WIFI] conectado com sucesso\n");
  return true;
}

// ====== MAIN ======
int main() {
  stdio_init_all();
  sleep_ms(2000);

  printf("Atividade 2 - Pico W + Telemetria\n");

  if (cyw43_arch_init()) {
    printf("[WIFI] falha ao inicializar cyw43\n");
    return 1;
  }
  cyw43_arch_enable_sta_mode();

  queue_init(&queue);
  init_button();
  init_adc();

  struct repeating_timer timer;
  add_repeating_timer_ms(5000, timer_callback, NULL, &timer);

  while (true) {
    if (button_event) {
      button_event = false;
      printf("[GPIO] botao mudou: %s\n", last_button_state == 0 ? "pressionado" : "solto");
      enqueue_button_telemetry();
    }

    if (adc_read_flag) {
      adc_read_flag = false;
      enqueue_analog_telemetry();
    }

    switch (state) {
      case STATE_BOOT:
        state = STATE_WIFI_CONNECTING;
        break;

      case STATE_WIFI_CONNECTING:
        if (wifi_connect()) {
          state = STATE_WIFI_READY;
        } else {
          retry_until_us = time_us_64() + 5000000ULL;
          state = STATE_RETRY_WAIT;
        }
        break;

      case STATE_WIFI_READY:
        if (!queue_empty(&queue)) {
          state = STATE_SEND_PENDING;
        }
        break;

      case STATE_SEND_PENDING: {
        telemetry_t item;
        if (queue_pop(&queue, &item)) {
          char json[256];
          telemetry_to_json(&item, json, sizeof(json));

          printf("[HTTP] enviando: %s\n", json);

          if (http_post_json(API_HOST, API_PORT, API_PATH, json)) {
            printf("[HTTP] envio OK\n");
            state = STATE_WIFI_READY;
          } else {
            printf("[HTTP] falha no envio, reencolando\n");
            queue_push(&queue, &item);
            retry_until_us = time_us_64() + 3000000ULL;
            state = STATE_RETRY_WAIT;
          }
        } else {
          state = STATE_WIFI_READY;
        }
        break;
      }

      case STATE_RETRY_WAIT:
        if (time_us_64() >= retry_until_us) {
          state = STATE_WIFI_READY;
        }
        break;
          }

    sleep_ms(20);
  }
}