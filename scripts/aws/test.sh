#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/oneshotsea-aws-test.XXXXXX")"
cleanup() {
  if [[ -d "$tmp_dir" &&
        "$tmp_dir" == "${TMPDIR:-/tmp}"/oneshotsea-aws-test.* ]]; then
    rm -rf -- "$tmp_dir"
  fi
}
trap cleanup EXIT

fail() {
  printf 'test failure: %s\n' "$*" >&2
  exit 1
}

digest="$(printf cache | shasum -a 256 | awk '{print $1}')"
instance='i-00000000000000000'
profile='expMath-ResearchInstanceProfile-JpspoidArRiA'

catalog="$("${SCRIPT_DIR}/catalog.sh" 2>&1)"
[[ "$catalog" == *c8g.4xlarge* && "$catalog" == *c8i.4xlarge* &&
   "$catalog" == *m8g.xlarge* ]] || fail 'catalog dry-run omits a CPU candidate'

cost="$("${SCRIPT_DIR}/cost.sh" --start 2026-08-01 --end 2026-08-02 2>&1)"
[[ "$cost" == *Project*OneShotSEA* && "$cost" == *get-cost-and-usage* ]] ||
  fail 'cost dry-run does not filter the OneShotSEA project tag'

benchmark="$(AWS_INSTANCE_ID="$instance" "${SCRIPT_DIR}/benchmark-sea.sh" \
  --run-id p125-sea-test --prime 101 --a 2 --b 3 --max-level 193 \
  --trace-cap 64 --threads 1,2,4 2>&1)"
[[ "$benchmark" == *CAS-free*SEA* && "$benchmark" == *threads=1,2,4* ]] ||
  fail 'SEA benchmark dry-run omits the bounded thread-scaling contract'

provision="$("${SCRIPT_DIR}/provision.sh" \
  --launch-id test-20260801 --instance-type m8g.xlarge \
  --ami-id ami-0263206814db4826a --subnet-id subnet-0800205b8fbcd6777 \
  --security-group-id sg-00000000000000000 \
  --iam-instance-profile "$profile" --associate-public-ip \
  --max-price-per-hour 0.20 --max-lifetime-minutes 30 2>&1)"
[[ "$provision" == *hard-stop*timer* && "$provision" == *'ingress=[]'* &&
   "$provision" != *key-name* ]] || fail 'provision dry-run violates keyless bounded contract'

launch="$(AWS_INSTANCE_ID="$instance" "${SCRIPT_DIR}/launch-worker.sh" \
  --run-id p125-test --run-kind benchmark \
  --prime 101 --worker-id 2 --worker-count 3 \
  --range-start 10 --range-end 21 --seed 7 --max-level 401 \
  --sea-threads 4 --table-dir data/modpoly/weber_f \
  --smooth-cache /opt/oneshotsea/caches/p125/smooth.cache \
  --smooth-cache-sha256 "$digest" --max-curves 1 2>&1)"
[[ "$launch" == *'assigned_range=[18,21)'* &&
   "$launch" == *'--range-start 10 --range-end 21'* &&
   "$launch" == *'--worker-id 2 --worker-count 3'* ]] ||
  fail 'launch dry-run changed the exactly-once partition contract'

  if AWS_INSTANCE_ID="$instance" "${SCRIPT_DIR}/launch-worker.sh" \
    --run-id unbounded --run-kind benchmark --prime 101 \
    --worker-id 0 --worker-count 1 --range-start 0 --range-end 1 \
    --seed 1 --max-level 11 --sea-threads 1 \
    --table-dir data/modpoly/weber_f \
    --smooth-cache /opt/oneshotsea/caches/test/smooth.cache \
    --smooth-cache-sha256 "$digest" >/dev/null 2>&1; then
  fail 'unbounded benchmark was accepted'
fi

if grep -E 'version[[:space:]]*\|[[:space:]]*head' "${SCRIPT_DIR}/deploy.sh" >/dev/null; then
  fail 'deploy version probe can trigger SIGPIPE under pipefail'
fi

python3 - "$PROJECT_ROOT" <<'PY'
import importlib.util
import pathlib
import sys

path = pathlib.Path(sys.argv[1]) / "scripts/aws/remote_worker.py"
spec = importlib.util.spec_from_file_location("remote_worker", path)
module = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(module)
assert [module.partition(10, 21, worker, 3) for worker in range(3)] == [
    (10, 14), (14, 18), (18, 21)
]
PY

counter="${tmp_dir}/ssm-count"
printf '0\n' >"$counter"
COUNTER="$counter" COMMON="${SCRIPT_DIR}/common.sh" bash -c '
  source "$COMMON"
  sleep() { :; }
  aws_cli() {
    if [[ "$1 $2" == "ssm send-command" ]]; then
      printf '\''{"Command":{"CommandId":"12345678-abcd"}}\n'\''
    elif [[ "$1 $2" == "ssm get-command-invocation" ]]; then
      value=$(cat "$COUNTER"); value=$((value + 1)); printf "%s\n" "$value" >"$COUNTER"
      if (( value == 1 )); then
        printf '\''{"Status":"InProgress"}\n'\''
      else
        printf '\''{"Status":"Success","StandardOutputContent":"done\\n"}\n'\''
      fi
    else
      return 2
    fi
  }
  [[ "$(ssm_run_command i-00000000000000000 "OneShotSEA test" true 10)" == done ]]
' || fail 'SSM polling did not survive an in-progress command'

fake_root="${tmp_dir}/remote"
deploy="${fake_root}/deployments/$(printf 'a%.0s' {1..40})"
mkdir -p "${deploy}/build" "${deploy}/data/modpoly/weber_f" \
  "${fake_root}/caches/test" "${tmp_dir}/bin"
printf '#!/usr/bin/env bash\nexit 0\n' >"${deploy}/build/oneshotsea"
chmod 755 "${deploy}/build/oneshotsea"
printf cache >"${fake_root}/caches/test/smooth.cache"
binary_sha="$(shasum -a 256 "${deploy}/build/oneshotsea" | awk '{print $1}')"
cache_sha="$(shasum -a 256 "${fake_root}/caches/test/smooth.cache" | awk '{print $1}')"
python3 - "$deploy" "$instance" "$binary_sha" <<'PY'
import json
import pathlib
import sys

deploy = pathlib.Path(sys.argv[1])
value = {
    "schema": "oneshotsea.aws-build.v1",
    "instance_id": sys.argv[2],
    "deployment_commit": "a" * 40,
    "binary_relative_path": "build/oneshotsea",
    "binary_sha256": sys.argv[3],
}
(deploy / "build-manifest.json").write_text(json.dumps(value) + "\n")
PY
ln -s "$deploy" "${fake_root}/current"
# The mock must evaluate its first argument when it runs, not while generated.
# shellcheck disable=SC2016
printf '#!/usr/bin/env bash\n[[ "$1" == has-session ]] && exit 1\nexit 0\n' >"${tmp_dir}/bin/tmux"
chmod 755 "${tmp_dir}/bin/tmux"
PATH="${tmp_dir}/bin:${PATH}" python3 "${SCRIPT_DIR}/remote_worker.py" \
  --root "$fake_root" --instance-id "$instance" --run-id integration \
  --run-kind benchmark --prime 101 --worker-id 2 --worker-count 3 \
  --range-start 10 --range-end 21 --seed 7 --max-level 11 --sea-threads 4 \
  --table-dir data/modpoly/weber_f \
  --smooth-cache "${fake_root}/caches/test/smooth.cache" \
  --smooth-cache-sha256 "$cache_sha" --max-curves 1 >"${tmp_dir}/remote.out"
jq -e '
  .schema == "oneshotsea.aws-worker.v1" and
  .global_range == {start:"10",end:"21",count:"11"} and
  .assigned_range == {start:"18",end:"21",count:"3"} and
  (.command_argv | index("--range-start") != null)
' "${fake_root}/runs/integration/worker-2/manifest.json" >/dev/null ||
  fail 'remote worker manifest does not bind the exact partition'

bash -n "${SCRIPT_DIR}"/*.sh
python3 -m py_compile "${SCRIPT_DIR}"/*.py
printf 'ok: AWS dry-run, bounded launch, exact sharding, SSM polling, and immutable worker tests passed\n'
