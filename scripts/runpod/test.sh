#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/oneshotsea-runpod-test.XXXXXX")"

cleanup() {
  if [[ -d "$tmp_dir" && "$tmp_dir" == "${TMPDIR:-/tmp}"/oneshotsea-runpod-test.* ]]; then
    rm -rf -- "$tmp_dir"
  fi
}
trap cleanup EXIT

fail() {
  printf 'FAIL: %s\n' "$*" >&2
  exit 1
}

for script in "${SCRIPT_DIR}"/*.sh; do
  bash -n "$script"
done
if command -v shellcheck >/dev/null 2>&1; then
  shellcheck -x -P "$SCRIPT_DIR" "${SCRIPT_DIR}"/*.sh
fi
PYTHONPYCACHEPREFIX="${tmp_dir}/pycache" python3 -m py_compile "${SCRIPT_DIR}/shard.py"

python3 "${SCRIPT_DIR}/shard.py" --global-start 100 --global-count 10 \
  --worker-id 0 --worker-count 3 --seed-base 700 >"${tmp_dir}/s0.json"
python3 "${SCRIPT_DIR}/shard.py" --global-start 100 --global-count 10 \
  --worker-id 1 --worker-count 3 --seed-base 700 >"${tmp_dir}/s1.json"
python3 "${SCRIPT_DIR}/shard.py" --global-start 100 --global-count 10 \
  --worker-id 2 --worker-count 3 --seed-base 700 >"${tmp_dir}/s2.json"
jq -e '.range_start == "100" and .range_end == "104" and .seed == "700"' "${tmp_dir}/s0.json" >/dev/null
jq -e '.range_start == "104" and .range_end == "107" and .seed == "701"' "${tmp_dir}/s1.json" >/dev/null
jq -e '.range_start == "107" and .range_end == "110" and .seed == "702"' "${tmp_dir}/s2.json" >/dev/null

secret_sentinel='RUNPOD_SECRET_MUST_NOT_APPEAR_7e948d'
export RUNPOD_API_KEY="$secret_sentinel"
export RUNPOD_POD_ID='pod_test_123'
export RUNPOD_SSH_HOST='203.0.113.10'
export RUNPOD_SSH_PORT=2222
export RUNPOD_SSH_USER=root
export RUNPOD_STATE_DIR="${tmp_dir}/state"

{
  "${SCRIPT_DIR}/catalog.sh"
  "${SCRIPT_DIR}/provision.sh" --gpu-id 'NVIDIA GeForce RTX 4090' --max-price-per-hour 1.00
  "${SCRIPT_DIR}/start.sh"
  "${SCRIPT_DIR}/status.sh" --remote --run-id smoke
  "${SCRIPT_DIR}/deploy.sh" --repo "$PROJECT_ROOT" --build-command 'cmake --build build -j2'
  "${SCRIPT_DIR}/launch-worker.sh" --run-id smoke --prime 101 --worker-id 0 \
    --worker-count 2 --range-start 0 --range-end 1000 --seed 77
  "${SCRIPT_DIR}/fetch.sh" --run-id smoke --destination "${tmp_dir}/fetch"
  "${SCRIPT_DIR}/stop.sh"
} >"${tmp_dir}/dry-run.out" 2>"${tmp_dir}/dry-run.err"

if rg -F "$secret_sentinel" "${tmp_dir}/dry-run.out" "${tmp_dir}/dry-run.err"; then
  fail "API key leaked in dry-run output"
fi
[[ ! -e "${tmp_dir}/fetch" ]] || fail "fetch dry-run created destination"
[[ ! -e "${tmp_dir}/state" ]] || fail "dry-run created operator state"
rg -F 'DRY-RUN:' "${tmp_dir}/dry-run.out" >/dev/null || fail "dry-run did not describe operations"

printf 'ok: syntax, shard coverage, dry-run no-write, and secret-redaction tests passed\n'
