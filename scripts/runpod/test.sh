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
if bash -c 'source "$1"; validate_positive_uint fixture 00' -- \
    "${SCRIPT_DIR}/common.sh" >"${tmp_dir}/zero-positive.out" \
    2>"${tmp_dir}/zero-positive.err"; then
  fail "positive integer validation accepted all-zero decimal text"
fi
rg -F 'must be positive' "${tmp_dir}/zero-positive.err" >/dev/null ||
  fail "all-zero positive-value rejection was not diagnosed"

for worker_id in 0 1 2; do
  python3 "${SCRIPT_DIR}/shard.py" --global-start 100 --global-count 10 \
    --worker-id "$worker_id" --worker-count 3 --seed 700 \
    >"${tmp_dir}/s${worker_id}.json"
done
python3 - "${tmp_dir}/s0.json" "${tmp_dir}/s1.json" "${tmp_dir}/s2.json" <<'PY'
import json
import sys

records = []
for path in sys.argv[1:]:
    with open(path, encoding="utf-8") as stream:
        records.append(json.load(stream))
for record in records:
    assert record["range_start"] == "100"
    assert record["range_end"] == "110"
    assert record["range_count"] == "10"
    assert record["seed"] == "700"
assignments = sorted(
    (int(record["assigned_range_start"]),
     int(record["assigned_range_end"]))
    for record in records
)
cursor = 100
for start, end in assignments:
    assert start == cursor
    assert start <= end
    cursor = end
assert cursor == 110
PY

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
  "${SCRIPT_DIR}/deploy.sh" --repo "$PROJECT_ROOT" --build-command 'make -j2 all'
  "${SCRIPT_DIR}/launch-worker.sh" --run-id smoke --run-kind benchmark \
    --prime 101 --worker-id 0 \
    --worker-count 2 --range-start 0 --range-end 1000 --seed 77 \
    --max-level 31 --sea-threads 1 --table-dir data/modpoly/weber_f \
    --smooth-cache /workspace/OneShotSEA/caches/p101.cache \
    --smooth-cache-sha256 0000000000000000000000000000000000000000000000000000000000000000 \
    --max-curves 1
  "${SCRIPT_DIR}/fetch.sh" --run-id smoke --destination "${tmp_dir}/fetch"
  "${SCRIPT_DIR}/stop.sh"
} >"${tmp_dir}/dry-run.out" 2>"${tmp_dir}/dry-run.err"

if rg -F "$secret_sentinel" "${tmp_dir}/dry-run.out" "${tmp_dir}/dry-run.err"; then
  fail "API key leaked in dry-run output"
fi
[[ ! -e "${tmp_dir}/fetch" ]] || fail "fetch dry-run created destination"
[[ ! -e "${tmp_dir}/state" ]] || fail "dry-run created operator state"
rg -F 'DRY-RUN:' "${tmp_dir}/dry-run.out" >/dev/null || fail "dry-run did not describe operations"

remote_root="${tmp_dir}/remote"
smooth_cache="${remote_root}/caches/p101.cache"
mkdir -p "${remote_root}/caches" "${tmp_dir}/cache-build"
ln -s "$PROJECT_ROOT" "${remote_root}/current"
"${PROJECT_ROOT}/build/oneshotsea" search \
  --p 101 --seed 17 --range-start 0 --range-end 1 \
  --worker-id 0 --worker-count 1 --max-level 31 \
  --table-dir "${PROJECT_ROOT}/data/modpoly/weber_f" \
  --smooth-cache "$smooth_cache" \
  --checkpoint "${tmp_dir}/cache-build/checkpoint.json" \
  --progress "${tmp_dir}/cache-build/progress.jsonl" \
  --certificate-out "${tmp_dir}/cache-build/certificate.txt" \
  --max-curves 0 >"${tmp_dir}/cache-build.out"
smooth_cache_sha256="$(python3 - "$smooth_cache" <<'PY'
import hashlib
import sys

with open(sys.argv[1], "rb") as stream:
    print(hashlib.sha256(stream.read()).hexdigest())
PY
)"

launcher_common=(
  --run-kind benchmark --prime 101 --worker-id 0 --worker-count 1
  --range-start 0 --range-end 1 --seed 17
  --max-level 31 --table-dir data/modpoly/weber_f --sea-threads 2
  --curve-family weber-f --curve-threads 2 --sea-level-telemetry 0
  --schoof-fallback 1 --skip-incomplete-curves 0 --smooth-coordinators 1
  --wall-time-limit-seconds 60
  --smooth-cache "$smooth_cache"
  --smooth-cache-sha256 "$smooth_cache_sha256"
)
if RUNPOD_REMOTE_ROOT="$remote_root" RUNPOD_TEST_REMOTE_ROOT="$remote_root" \
    "${SCRIPT_DIR}/launch-worker.sh" --run-id zero-bound \
    --run-kind benchmark --prime 101 --worker-id 0 --worker-count 1 \
    --range-start 0 --range-end 1 --seed 17 --max-level 31 --sea-threads 1 \
    --table-dir data/modpoly/weber_f --smooth-cache "$smooth_cache" \
    --smooth-cache-sha256 "$smooth_cache_sha256" --max-curves 00 \
    >"${tmp_dir}/zero-bound.out" 2>"${tmp_dir}/zero-bound.err"; then
  fail "benchmark launcher accepted an all-zero curve bound without timeout"
fi
rg -F 'benchmark runs require positive' "${tmp_dir}/zero-bound.err" >/dev/null ||
  fail "all-zero benchmark bound rejection was not diagnosed"

RUNPOD_REMOTE_ROOT="$remote_root" RUNPOD_TEST_REMOTE_ROOT="$remote_root" \
  "${SCRIPT_DIR}/launch-worker.sh" --run-id local-dry \
  "${launcher_common[@]}" --max-curves 1 \
  >"${tmp_dir}/launcher-dry.out" 2>"${tmp_dir}/launcher-dry.err"

if rg -e 'build/oneshot-sea|--prime|--progress-jsonl|--result|--resume-from' \
    "${tmp_dir}/launcher-dry.out"; then
  fail "launcher emitted a legacy executable or option"
fi
for expected in 'build/oneshotsea search' '--p 101' '--max-level 31' \
                '--smooth-cache-sha256' '--progress' '--certificate-out'; do
  rg -F -- "$expected" "${tmp_dir}/launcher-dry.out" >/dev/null ||
    fail "launcher dry-run omitted: $expected"
done

python3 - "${tmp_dir}/launcher-dry.out" <<'PY'
import shlex
import subprocess
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    for line in stream:
        if not line.startswith("DRY-RUN: "):
            continue
        command = shlex.split(line[len("DRY-RUN: "):])
        executable = next(
            (index for index, value in enumerate(command)
             if value.endswith("/build/oneshotsea")), None
        )
        if executable is not None:
            subprocess.run(command[executable:], check=True,
                           stdout=subprocess.DEVNULL)
            break
    else:
        raise SystemExit("no executable search command in launcher dry-run")
PY

for forbidden in --search-arg --checkpoint; do
  if RUNPOD_REMOTE_ROOT="$remote_root" RUNPOD_TEST_REMOTE_ROOT="$remote_root" \
      "${SCRIPT_DIR}/launch-worker.sh" --run-id forbidden \
      "${launcher_common[@]}" "$forbidden" forbidden-value \
      >"${tmp_dir}/forbidden.out" 2>"${tmp_dir}/forbidden.err"; then
    fail "launcher accepted forbidden override: $forbidden"
  fi
  rg -F 'unknown argument' "${tmp_dir}/forbidden.err" >/dev/null ||
    fail "launcher did not clearly reject: $forbidden"
done

mock_bin="${tmp_dir}/mock-bin"
mkdir -p "$mock_bin"
# The single-quoted parameter references belong to the generated mock script.
# shellcheck disable=SC2016
printf '%s\n' \
  '#!/usr/bin/env bash' \
  'set -euo pipefail' \
  'remote_command=""' \
  'for argument in "$@"; do remote_command="$argument"; done' \
  'exec bash -c "$remote_command"' \
  >"${mock_bin}/ssh"
# shellcheck disable=SC2016
printf '%s\n' \
  '#!/usr/bin/env bash' \
  'set -euo pipefail' \
  'case "${1:-}" in' \
  '  has-session) exit 1 ;;' \
  '  new-session) exit 0 ;;' \
  '  *) exit 2 ;;' \
  'esac' \
  >"${mock_bin}/tmux"
chmod 700 "${mock_bin}/ssh" "${mock_bin}/tmux"

if ! PATH="${mock_bin}:${PATH}" RUNPOD_REMOTE_ROOT="$remote_root" \
    RUNPOD_TEST_REMOTE_ROOT="$remote_root" \
    "${SCRIPT_DIR}/launch-worker.sh" --run-id resume-test \
    "${launcher_common[@]}" --max-curves 1 --execute \
    >"${tmp_dir}/launch-execute.out" 2>"${tmp_dir}/launch-execute.err"; then
  tail -n 200 "${tmp_dir}/launch-execute.out" >&2
  tail -n 200 "${tmp_dir}/launch-execute.err" >&2
  if [[ -f "${remote_root}/runs/resume-test/worker-0/worker.log" ]]; then
    tail -n 200 "${remote_root}/runs/resume-test/worker-0/worker.log" >&2
  fi
  fail "executed launcher failed"
fi

worker_dir="${remote_root}/runs/resume-test/worker-0"
manifest="${worker_dir}/manifest.json"
command_file="${worker_dir}/command.sh"
# The wrapper treats the checkpoint as opaque; native checkpoint validation is
# covered by the search tests. This fixture exercises only immutable relaunch.
printf '{}\n' >"${worker_dir}/checkpoint.json"
[[ -s "$manifest" && -s "$command_file" && -s "${worker_dir}/checkpoint.json" ]] ||
  fail "executed launcher did not create durable worker state"
jq -e '
  .schema == "oneshotsea.runpod-worker.v3" and
  .run_kind == "benchmark" and
  .global_range == {"start":"0","end":"1","count":"1"} and
  .assigned_range == {"start":"0","end":"1","count":"1"} and
  (.deployment_commit | test("^[0-9a-f]{40}$")) and
  (.binary_sha256 | test("^[0-9a-f]{64}$")) and
  .wall_time_limit_seconds == 60 and
  .command_argv[1] == "search" and
  (.command_argv | index("--curve-family") != null) and
  (.command_argv | index("--curve-threads") != null) and
  (.command_argv | index("--sea-level-telemetry") != null) and
  (.command_argv | index("--schoof-fallback") != null) and
  (.command_argv | index("--skip-incomplete-curves") != null) and
  (.command_argv | index("--smooth-coordinators") != null) and
  (.command_argv | index("--sea-threads") != null) and
  (.command_argv | index("--smooth-cache-sha256") != null) and
  (.command_argv | index("--progress") != null) and
  (.command_argv | index("--certificate-out") != null)
' "$manifest" >/dev/null || fail "worker manifest did not bind the complete command"
rg -F -- 'timeout --signal=TERM --kill-after=60 60' "$command_file" >/dev/null ||
  fail "worker command did not retain the wall-time bound"
rg -F -- '/usr/bin/time -a -v -o' "$command_file" >/dev/null ||
  fail "worker command did not append resource usage across attempts"
rg -F -- 'attempt_start utc=' "$command_file" >/dev/null ||
  fail "worker command did not delimit resource records by attempt"

manifest_before="$(python3 - "$manifest" "$command_file" <<'PY'
import hashlib
import sys

for path in sys.argv[1:]:
    with open(path, "rb") as stream:
        print(hashlib.sha256(stream.read()).hexdigest())
PY
)"
PATH="${mock_bin}:${PATH}" RUNPOD_REMOTE_ROOT="$remote_root" \
  RUNPOD_TEST_REMOTE_ROOT="$remote_root" \
  "${SCRIPT_DIR}/launch-worker.sh" --run-id resume-test \
  "${launcher_common[@]}" --max-curves 1 --resume --execute \
  >"${tmp_dir}/launch-resume.out"
manifest_after="$(python3 - "$manifest" "$command_file" <<'PY'
import hashlib
import sys

for path in sys.argv[1:]:
    with open(path, "rb") as stream:
        print(hashlib.sha256(stream.read()).hexdigest())
PY
)"
[[ "$manifest_before" == "$manifest_after" ]] ||
  fail "identical resume changed the manifest or command"

if PATH="${mock_bin}:${PATH}" RUNPOD_REMOTE_ROOT="$remote_root" \
    RUNPOD_TEST_REMOTE_ROOT="$remote_root" \
    "${SCRIPT_DIR}/launch-worker.sh" --run-id resume-test \
    "${launcher_common[@]}" --max-curves 2 --resume --execute \
    >"${tmp_dir}/resume-mismatch.out" 2>"${tmp_dir}/resume-mismatch.err"; then
  fail "resume accepted a changed command"
fi
rg -F 'retained command file changed' \
  "${tmp_dir}/resume-mismatch.err" >/dev/null ||
  fail "resume command mismatch was not diagnosed"

cp "$command_file" "${tmp_dir}/command.sh.authenticated"
printf '# tampered\n' >>"$command_file"
if PATH="${mock_bin}:${PATH}" RUNPOD_REMOTE_ROOT="$remote_root" \
    RUNPOD_TEST_REMOTE_ROOT="$remote_root" \
    "${SCRIPT_DIR}/launch-worker.sh" --run-id resume-test \
    "${launcher_common[@]}" --max-curves 1 --resume --execute \
    >"${tmp_dir}/resume-tamper.out" 2>"${tmp_dir}/resume-tamper.err"; then
  fail "resume accepted a changed retained command file"
fi
rg -F 'retained command file changed' \
  "${tmp_dir}/resume-tamper.err" >/dev/null ||
  fail "resume did not diagnose a changed retained command file"
mv "${tmp_dir}/command.sh.authenticated" "$command_file"

printf 'ok: RunPod syntax, exact sharding, CLI launch, immutable resume, no-write, and secret-redaction tests passed\n'
