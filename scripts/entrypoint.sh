#!/usr/bin/env bash
set -euo pipefail

HOST="${SERVER_HOST:-0.0.0.0}"
PORT="${SERVER_PORT:-8443}"
CERT_FILE="${SSL_CERT_FILE:-/app/certs/server.crt}"
KEY_FILE="${SSL_KEY_FILE:-/app/certs/server.key}"

if [[ ! -f "${CERT_FILE}" || ! -f "${KEY_FILE}" ]]; then
  echo "[entrypoint] TLS files not found. Generating self-signed certificate..."
  mkdir -p "$(dirname "${CERT_FILE}")"
  mkdir -p "$(dirname "${KEY_FILE}")"
  openssl req -x509 -nodes \
    -newkey rsa:2048 \
    -keyout "${KEY_FILE}" \
    -out "${CERT_FILE}" \
    -days 3650 \
    -subj "/CN=localhost"
fi

echo "[entrypoint] Starting DockerRoboshopServer on ${HOST}:${PORT}"
exec /app/DockerRoboshopServer \
  --host "${HOST}" \
  --port "${PORT}" \
  --cert "${CERT_FILE}" \
  --key "${KEY_FILE}"
