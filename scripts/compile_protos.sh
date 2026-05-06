#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

PROTO_VERSION="v1"
PROTO_DIR="${REPO_ROOT}/proto"
OUT_DIR="${REPO_ROOT}/generated/proto/${PROTO_VERSION}/cpp"

mkdir -p "${OUT_DIR}"

protoc \
  --proto_path="${PROTO_DIR}" \
  --cpp_out="${OUT_DIR}" \
  "${PROTO_DIR}/ingress.proto"

echo "Generated C++ protobuf sources in ${OUT_DIR}"
