import http from 'k6/http';
import { check } from 'k6';

const BASE_URL = __ENV.BASE_URL || 'http://localhost:8080';

export const options = {
  stages: [
    { duration: '30s', target: 3000 },
    { duration: '30s', target: 3300 },
    { duration: '30s', target: 3600 },
    { duration: '30s', target: 3900 },
    { duration: '10s', target: 0 },
  ],
  thresholds: {
    http_req_failed: [
      { threshold: 'rate<0.01', abortOnFail: true, delayAbortEval: '30s' },
    ],
    http_req_duration: [
      { threshold: 'p(95)<1000', abortOnFail: true, delayAbortEval: '30s' },
    ],
    checks: ['rate>0.99'],
  },
  summaryTrendStats: ['avg', 'min', 'med', 'max', 'p(90)', 'p(95)', 'p(99)'],
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

  const res = http.post(`${BASE_URL}/telemetry`, payload, {
    headers: { 'Content-Type': 'application/json' },
    tags: { endpoint: 'telemetry' },
  });

  check(res, {
    'status 202': (r) => r.status === 202,
  });
}
