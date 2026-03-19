#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 4 ]]; then
  echo "Usage: $0 <region> <namespace> <username> <password_or_token> [image_name] [tag]"
  echo "Example: $0 cn-north-4 myteam user token docker-roboshop-server 1.0.0"
  exit 1
fi

REGION="$1"
NAMESPACE="$2"
USERNAME="$3"
PASSWORD="$4"
IMAGE_NAME="${5:-docker-roboshop-server}"
TAG="${6:-1.0.0}"

REGISTRY="swr.${REGION}.myhuaweicloud.com"
TARGET_IMAGE="${REGISTRY}/${NAMESPACE}/${IMAGE_NAME}:${TAG}"

echo "[push] Login to ${REGISTRY}"
docker login -u "${USERNAME}" -p "${PASSWORD}" "${REGISTRY}"

echo "[push] Build local image ${IMAGE_NAME}:${TAG}"
docker build -t "${IMAGE_NAME}:${TAG}" .

echo "[push] Tag image as ${TARGET_IMAGE}"
docker tag "${IMAGE_NAME}:${TAG}" "${TARGET_IMAGE}"

echo "[push] Push ${TARGET_IMAGE}"
docker push "${TARGET_IMAGE}"

echo "[push] Done: ${TARGET_IMAGE}"
