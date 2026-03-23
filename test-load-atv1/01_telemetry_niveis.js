import http from 'k6/http';
import { check, sleep } from 'k6';

export const options = {
  stages: [
    { duration: '30s', target: 5 },
    { duration: '30s', target: 20 },
    { duration: '30s', target: 50 },
    { duration: '10s', target: 0 },
  ],
  thresholds: {
    http_req_failed: ['rate<0.05'],
    http_req_duration: ['p(95)<1000'],
  },
};

function buildPayload() {
  const isAnalog = Math.random() < 0.7;

  if (isAnalog) {
    return {
      device_id: `pico-w-${__VU}`,
      timestamp: new Date().toISOString(),
      sensor_type: 'potentiometer',
      reading_type: 'analog',
      value: Number((Math.random() * 100).toFixed(2)),
    };
  }

  return {
    device_id: `pico-w-${__VU}`,
    timestamp: new Date().toISOString(),
    sensor_type: 'button',
    reading_type: 'discrete',
    value: Math.random() < 0.5 ? 0 : 1,
  };
}

export default function () {
  const payload = JSON.stringify(buildPayload());

  const params = {
    headers: {
      'Content-Type': 'application/json',
    },
  };

  const res = http.post('http://localhost:8080/telemetry', payload, params);

  check(res, {
    'status 202': (r) => r.status === 202,
    'resposta com sucesso': (r) =>
      r.body && r.body.includes('mensagem enfileirada com sucesso'),
  });

  sleep(1);
}
