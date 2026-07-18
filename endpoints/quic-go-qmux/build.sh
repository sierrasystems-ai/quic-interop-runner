#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
IMAGE="${QMUX_IMAGE:-quic-go-qmux-interop:local}"
PLATFORM="${QMUX_PLATFORM:-linux/amd64}"

echo "Building ${IMAGE} (platform ${PLATFORM}) from ${ROOT}"
docker build \
  --platform "${PLATFORM}" \
  -t "${IMAGE}" \
  "${ROOT}"

echo "Built ${IMAGE}"
