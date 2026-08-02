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

if bash -c 'source "$1"; validate_positive_uint fixture 00' -- \
    "${SCRIPT_DIR}/common.sh" >/dev/null 2>&1; then
  fail 'positive integer validation accepted all-zero decimal text'
fi

catalog="$("${SCRIPT_DIR}/catalog.sh" 2>&1)"
[[ "$catalog" == *c8g.4xlarge* && "$catalog" == *c8i.4xlarge* &&
   "$catalog" == *m8g.xlarge* ]] || fail 'catalog dry-run omits a CPU candidate'

cost="$("${SCRIPT_DIR}/cost.sh" --start 2026-08-01 --end 2026-08-02 2>&1)"
[[ "$cost" == *Project*OneShotSEA* && "$cost" == *get-cost-and-usage* ]] ||
  fail 'cost dry-run does not filter the OneShotSEA project tag'
task_cost="$("${SCRIPT_DIR}/cost.sh" --start 2026-08-01 --end 2026-08-02 \
  --task p125-production-shard 2>&1)"
[[ "$task_cost" == *Project*OneShotSEA* && "$task_cost" == *Task*p125-production-shard* ]] ||
  fail 'cost dry-run does not combine Project and Task tags'

cache_dry="$(AWS_INSTANCE_ID="$instance" "${SCRIPT_DIR}/prepare-cache.sh" \
  --cache-id p125-cache --prime 101 --max-level 401 \
  --table-dir data/modpoly/weber_f --expected-cache-sha256 "$digest" 2>&1)"
[[ "$cache_dry" == *"require rebuilt smooth cache SHA-256=${digest}"* &&
   "$cache_dry" == *'worker=1/2 assigned_range=[1,1) (zero curves)'* ]] ||
  fail 'cache dry-run does not pin its digest and empty assignment'

benchmark="$(AWS_INSTANCE_ID="$instance" "${SCRIPT_DIR}/benchmark-sea.sh" \
  --run-id p125-sea-test --prime 101 --a 2 --b 3 --max-level 193 \
  --trace-cap 64 --threads 1,2,4 2>&1)"
[[ "$benchmark" == *CAS-free*SEA* && "$benchmark" == *threads=1,2,4* ]] ||
  fail 'SEA benchmark dry-run omits the bounded thread-scaling contract'

provision="$("${SCRIPT_DIR}/provision.sh" \
  --launch-id test-20260801 --instance-type m8g.xlarge \
  --task-tag p125-coordinator-ab \
  --ami-id ami-0263206814db4826a --subnet-id subnet-0800205b8fbcd6777 \
  --security-group-id sg-00000000000000000 \
  --iam-instance-profile "$profile" --associate-public-ip \
  --max-price-per-hour 0.20 --max-lifetime-minutes 30 2>&1)"
[[ "$provision" == *guest-lifetime*timer* && "$provision" == *'ingress=[]'* &&
   "$provision" == *'instance+root-volume'*'Task=p125-coordinator-ab'* &&
   "$provision" != *key-name* ]] || fail 'provision dry-run violates keyless bounded contract'

terminate_dry="$(AWS_INSTANCE_ID="$instance" "${SCRIPT_DIR}/terminate.sh" \
  --launch-id test-20260801 --task-tag p125-coordinator-ab \
  --allow-unfetched 2>&1)"
[[ "$terminate_dry" == *Project=OneShotSEA*LaunchId=test-20260801*Task=p125-coordinator-ab* ]] ||
  fail 'terminate dry-run does not bind destructive tag identity'

launch="$(AWS_INSTANCE_ID="$instance" "${SCRIPT_DIR}/launch-worker.sh" \
  --run-id p125-test --run-kind benchmark \
  --prime 101 --worker-id 2 --worker-count 3 \
  --range-start 10 --range-end 21 --seed 7 --max-level 401 \
  --curve-family x1-27 --x1-require-point4 1 --curve-threads 10 \
  --sea-level-telemetry 0 --schoof-fallback 1 --skip-incomplete-curves 0 \
  --smooth-coordinators 1 \
  --sea-threads 4 --table-dir data/modpoly/weber_f \
  --smooth-cache /opt/oneshotsea/caches/p125/smooth.cache \
  --smooth-cache-sha256 "$digest" --max-curves 1 2>&1)"
[[ "$launch" == *'assigned_range=[18,21)'* &&
   "$launch" == *'--range-start 10 --range-end 21'* &&
   "$launch" == *'--worker-id 2 --worker-count 3'* &&
   "$launch" == *'--curve-family x1-27 --x1-require-point4 1'* &&
   "$launch" == *'--curve-threads 10 --sea-level-telemetry 0'* &&
   "$launch" == *'--schoof-fallback 1 --skip-incomplete-curves 0'* &&
   "$launch" == *'--smooth-coordinators 1'* ]] ||
  fail 'launch dry-run changed the exactly-once partition contract'

if AWS_INSTANCE_ID="$instance" "${SCRIPT_DIR}/launch-worker.sh" \
  --run-id invalid-options --run-kind benchmark --prime 101 \
  --worker-id 0 --worker-count 1 --range-start 0 --range-end 1 \
  --seed 1 --max-level 11 --sea-threads 1 --curve-family weber-f \
  --x1-require-point4 1 --table-dir data/modpoly/weber_f \
  --smooth-cache /opt/oneshotsea/caches/test/smooth.cache \
  --smooth-cache-sha256 "$digest" --max-curves 1 >/dev/null 2>&1; then
  fail 'incompatible curve-family options were accepted'
fi

  if AWS_INSTANCE_ID="$instance" "${SCRIPT_DIR}/launch-worker.sh" \
    --run-id unbounded --run-kind benchmark --prime 101 \
    --worker-id 0 --worker-count 1 --range-start 0 --range-end 1 \
    --seed 1 --max-level 11 --sea-threads 1 \
    --table-dir data/modpoly/weber_f \
    --smooth-cache /opt/oneshotsea/caches/test/smooth.cache \
    --smooth-cache-sha256 "$digest" --max-curves 00 \
    --wall-time-limit-seconds 60 >/dev/null 2>&1; then
  fail 'zero-curve benchmark was accepted despite a wall timeout'
fi

if AWS_INSTANCE_ID="$instance" "${SCRIPT_DIR}/launch-worker.sh" \
  --run-id invalid-topology --run-kind benchmark --prime 101 \
  --worker-id 0 --worker-count 1 --range-start 0 --range-end 1 \
  --seed 1 --max-level 11 --sea-threads 1 --curve-threads 1 \
  --smooth-coordinators 2 --table-dir data/modpoly/weber_f \
  --smooth-cache /opt/oneshotsea/caches/test/smooth.cache \
  --smooth-cache-sha256 "$digest" --max-curves 1 >/dev/null 2>&1; then
  fail 'coordinator count larger than curve thread count was accepted'
fi

if AWS_INSTANCE_ID="$instance" "${SCRIPT_DIR}/launch-worker.sh" \
  --run-id unbounded-production --run-kind production --prime 101 \
  --worker-id 0 --worker-count 1 --range-start 0 --range-end 1 \
  --seed 1 --max-level 11 --sea-threads 1 \
  --table-dir data/modpoly/weber_f \
  --smooth-cache /opt/oneshotsea/caches/test/smooth.cache \
  --smooth-cache-sha256 "$digest" --max-curves 1 \
  --wall-time-limit-seconds 00 \
  >/dev/null 2>&1; then
  fail 'production worker without fetch-margin wall limit was accepted'
fi

if AWS_INSTANCE_ID="$instance" "${SCRIPT_DIR}/launch-worker.sh" \
  --run-id zero-production --run-kind production --prime 101 \
  --worker-id 0 --worker-count 1 --range-start 0 --range-end 1 \
  --seed 1 --max-level 11 --sea-threads 1 \
  --table-dir data/modpoly/weber_f \
  --smooth-cache /opt/oneshotsea/caches/test/smooth.cache \
  --smooth-cache-sha256 "$digest" --max-curves 00 \
  --wall-time-limit-seconds 60 >/dev/null 2>&1; then
  fail 'production worker accepted a zero-curve cap'
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
    "environment_sha256": "",
}
(deploy / "build-manifest.json").write_text(json.dumps(value) + "\n")
PY
printf 'environment\n' >"${deploy}/environment.txt"
printf 'build log\n' >"${deploy}/build.log"
environment_sha="$(shasum -a 256 "${deploy}/environment.txt" | awk '{print $1}')"
jq --arg sha "$environment_sha" '.environment_sha256 = $sha' \
  "${deploy}/build-manifest.json" >"${deploy}/build-manifest.json.tmp"
mv "${deploy}/build-manifest.json.tmp" "${deploy}/build-manifest.json"
printf 'cache command\n' >"${fake_root}/caches/test/command.sh"
printf 'cache build log\n' >"${fake_root}/caches/test/build.log"
python3 - "${fake_root}/caches/test/manifest.json" "$binary_sha" "$cache_sha" <<'PY'
import json
import pathlib
import sys

path, binary_sha, cache_sha = sys.argv[1:]
pathlib.Path(path).write_text(json.dumps({
    "schema": "oneshotsea.aws-cache.v1", "prime": "101", "max_level": 11,
    "table_dir": "data/modpoly/weber_f", "deployment_commit": "a" * 40,
    "binary_sha256": binary_sha, "smooth_cache_sha256": cache_sha,
    "expected_smooth_cache_sha256": cache_sha,
}) + "\n")
PY
ln -s "$deploy" "${fake_root}/current"
# The mock must evaluate its first argument when it runs, not while generated.
# shellcheck disable=SC2016
printf '#!/usr/bin/env bash\n[[ "$1" == has-session ]] && exit 1\nexit 0\n' >"${tmp_dir}/bin/tmux"
chmod 755 "${tmp_dir}/bin/tmux"
fake_aws_state="${tmp_dir}/fake-aws-state"
printf 'running\n' >"$fake_aws_state"
cat >"${tmp_dir}/bin/aws" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
operation="${4:-} ${5:-}"
case "$operation" in
  'ec2 describe-instances')
    state="$(cat "$FAKE_AWS_STATE")"
    printf '{"Reservations":[{"Instances":[{"InstanceId":"i-00000000000000000","InstanceType":"m8g.xlarge","ImageId":"ami-test","State":{"Name":"%s"},"LaunchTime":"2026-08-01T00:00:00Z","Placement":{"AvailabilityZone":"us-east-2a"},"Tags":[{"Key":"Project","Value":"OneShotSEA"},{"Key":"LaunchId","Value":"test-20260801"},{"Key":"Task","Value":"p125-coordinator-ab"}]}]}]}\n' "$state"
    ;;
  'ec2 terminate-instances')
    printf 'shutting-down\n' >"$FAKE_AWS_STATE"
    printf '{"TerminatingInstances":[{"InstanceId":"i-00000000000000000","PreviousState":{"Name":"running"},"CurrentState":{"Name":"shutting-down"}}]}\n'
    ;;
  'ec2 wait')
    printf 'terminated\n' >"$FAKE_AWS_STATE"
    ;;
  *) exit 2 ;;
esac
SH
chmod 755 "${tmp_dir}/bin/aws"
ONESHOTSEA_AWS_STATE_DIR="${tmp_dir}/aws-state" FAKE_AWS_STATE="$fake_aws_state" \
  PATH="${tmp_dir}/bin:${PATH}" AWS_INSTANCE_ID="$instance" \
  "${SCRIPT_DIR}/terminate.sh" --launch-id test-20260801 \
  --task-tag p125-coordinator-ab --allow-unfetched --execute \
  >"${tmp_dir}/terminate.out" 2>"${tmp_dir}/terminate.err" || {
    sed -n '1,80p' "${tmp_dir}/terminate.err" >&2
    fail 'tag-bound emergency termination did not execute against the mock'
  }
[[ "$(cat "$fake_aws_state")" == terminated ]] ||
  fail 'mock termination did not reach terminated state'
printf 'running\n' >"$fake_aws_state"
fetch_dir="${tmp_dir}/fetch-evidence"
mkdir -p "$fetch_dir"
printf 'artifact\n' >"${fetch_dir}/artifact.txt"
tar -czf "${fetch_dir}/retrieval.tar.gz" -C "$fetch_dir" artifact.txt
retrieval_sha="$(shasum -a 256 "${fetch_dir}/retrieval.tar.gz" | awk '{print $1}')"
printf '{"schema":"oneshotsea.aws-fetch.v1","run_id":"p125-test","instance_id":"%s","remote_archive_sha256":"%s"}\n' \
  "$instance" "$retrieval_sha" >"${fetch_dir}/fetch-metadata.json"
(
  cd "$fetch_dir"
  shasum -a 256 artifact.txt fetch-metadata.json retrieval.tar.gz
) >"${fetch_dir}/SHA256SUMS"
ONESHOTSEA_AWS_STATE_DIR="${tmp_dir}/aws-state" FAKE_AWS_STATE="$fake_aws_state" \
  PATH="${tmp_dir}/bin:${PATH}" AWS_INSTANCE_ID="$instance" \
  "${SCRIPT_DIR}/terminate.sh" --launch-id test-20260801 \
  --task-tag p125-coordinator-ab --fetched-run p125-test \
  --fetch-directory "$fetch_dir" --execute >/dev/null 2>&1 ||
  fail 'verified fetched-run termination did not execute against the mock'
[[ "$(cat "$fake_aws_state")" == terminated ]] ||
  fail 'fetched-run termination did not reach terminated state'
printf 'running\n' >"$fake_aws_state"
cp "${fetch_dir}/retrieval.tar.gz" "${tmp_dir}/retrieval.tar.gz.saved"
printf 'tamper\n' >>"${fetch_dir}/retrieval.tar.gz"
if ONESHOTSEA_AWS_STATE_DIR="${tmp_dir}/aws-state" FAKE_AWS_STATE="$fake_aws_state" \
  PATH="${tmp_dir}/bin:${PATH}" AWS_INSTANCE_ID="$instance" \
  "${SCRIPT_DIR}/terminate.sh" --launch-id test-20260801 \
  --task-tag p125-coordinator-ab --fetched-run p125-test \
  --fetch-directory "$fetch_dir" --execute >/dev/null 2>&1; then
  fail 'terminate accepted a retrieval archive that did not match fetch metadata'
fi
[[ "$(cat "$fake_aws_state")" == running ]] ||
  fail 'tampered retrieval archive reached the terminate API'
mv "${tmp_dir}/retrieval.tar.gz.saved" "${fetch_dir}/retrieval.tar.gz"
grep -v 'retrieval.tar.gz$' "${fetch_dir}/SHA256SUMS" >"${fetch_dir}/SHA256SUMS.no-archive"
mv "${fetch_dir}/SHA256SUMS.no-archive" "${fetch_dir}/SHA256SUMS"
if ONESHOTSEA_AWS_STATE_DIR="${tmp_dir}/aws-state" FAKE_AWS_STATE="$fake_aws_state" \
  PATH="${tmp_dir}/bin:${PATH}" AWS_INSTANCE_ID="$instance" \
  "${SCRIPT_DIR}/terminate.sh" --launch-id test-20260801 \
  --task-tag p125-coordinator-ab --fetched-run p125-test \
  --fetch-directory "$fetch_dir" --execute >/dev/null 2>&1; then
  fail 'terminate accepted fetch evidence whose checksum manifest omitted the archive'
fi
[[ "$(cat "$fake_aws_state")" == running ]] ||
  fail 'archive-omitting checksum manifest reached the terminate API'
if ONESHOTSEA_AWS_STATE_DIR="${tmp_dir}/aws-state" FAKE_AWS_STATE="$fake_aws_state" \
  PATH="${tmp_dir}/bin:${PATH}" AWS_INSTANCE_ID="$instance" \
  "${SCRIPT_DIR}/terminate.sh" --launch-id wrong-launch \
  --task-tag p125-coordinator-ab --allow-unfetched --execute \
  >/dev/null 2>&1; then
  fail 'terminate accepted a mismatched LaunchId tag'
fi
[[ "$(cat "$fake_aws_state")" == running ]] ||
  fail 'mismatched LaunchId reached the terminate API'
PATH="${tmp_dir}/bin:${PATH}" python3 "${SCRIPT_DIR}/remote_worker.py" \
  --root "$fake_root" --instance-id "$instance" --run-id integration \
  --run-kind benchmark --prime 101 --worker-id 2 --worker-count 3 \
  --range-start 10 --range-end 21 --seed 7 --max-level 11 --sea-threads 4 \
  --curve-family x1-27 --x1-require-point4 1 --curve-threads 10 \
  --sea-level-telemetry 0 --schoof-fallback 1 --skip-incomplete-curves 0 \
  --smooth-coordinators 1 \
  --table-dir data/modpoly/weber_f \
  --smooth-cache "${fake_root}/caches/test/smooth.cache" \
  --smooth-cache-sha256 "$cache_sha" --max-curves 1 >"${tmp_dir}/remote.out"
jq -e '
  .schema == "oneshotsea.aws-worker.v1" and
  (.provenance_manifest_sha256 | test("^[0-9a-f]{64}$")) and
  (.command_sha256 | test("^[0-9a-f]{64}$")) and
  .global_range == {start:"10",end:"21",count:"11"} and
  .assigned_range == {start:"18",end:"21",count:"3"} and
  (.command_argv | index("--range-start") != null) and
  (.command_argv | index("--curve-family")) as $family |
  .command_argv[$family + 1] == "x1-27" and
  (.command_argv | index("--x1-require-point4")) as $point4 |
  .command_argv[$point4 + 1] == "1" and
  (.command_argv | index("--curve-threads")) as $curves |
  .command_argv[$curves + 1] == "10" and
  (.command_argv | index("--sea-level-telemetry")) as $telemetry |
  .command_argv[$telemetry + 1] == "0" and
  (.command_argv | index("--schoof-fallback")) as $schoof |
  .command_argv[$schoof + 1] == "1" and
  (.command_argv | index("--skip-incomplete-curves")) as $skip |
  .command_argv[$skip + 1] == "0" and
  (.command_argv | index("--smooth-coordinators")) as $coordinators |
  .command_argv[$coordinators + 1] == "1"
' "${fake_root}/runs/integration/worker-2/manifest.json" >/dev/null ||
  fail 'remote worker manifest does not bind the exact partition'
jq -e '.schema == "oneshotsea.aws-worker-provenance.v1" and
  (.files | keys | length == 7)' \
  "${fake_root}/runs/integration/worker-2/provenance/manifest.json" >/dev/null ||
  fail 'remote worker did not retain build/cache provenance'

printf '{"checkpoint":"nonempty"}\n' >"${fake_root}/runs/integration/worker-2/checkpoint.json"
PATH="${tmp_dir}/bin:${PATH}" python3 "${SCRIPT_DIR}/remote_worker.py" \
  --root "$fake_root" --instance-id "$instance" --run-id integration \
  --run-kind benchmark --prime 101 --worker-id 2 --worker-count 3 \
  --range-start 10 --range-end 21 --seed 7 --max-level 11 --sea-threads 4 \
  --curve-family x1-27 --x1-require-point4 1 --curve-threads 10 \
  --sea-level-telemetry 0 --schoof-fallback 1 --skip-incomplete-curves 0 \
  --smooth-coordinators 1 --table-dir data/modpoly/weber_f \
  --smooth-cache "${fake_root}/caches/test/smooth.cache" \
  --smooth-cache-sha256 "$cache_sha" --max-curves 1 --resume \
  >"${tmp_dir}/resume.out" || fail 'identical semantic options did not resume'

command_path="${fake_root}/runs/integration/worker-2/command.sh"
cp "$command_path" "${tmp_dir}/command.sh.saved"
printf '# mutation\n' >>"$command_path"
if PATH="${tmp_dir}/bin:${PATH}" python3 "${SCRIPT_DIR}/remote_worker.py" \
  --root "$fake_root" --instance-id "$instance" --run-id integration \
  --run-kind benchmark --prime 101 --worker-id 2 --worker-count 3 \
  --range-start 10 --range-end 21 --seed 7 --max-level 11 --sea-threads 4 \
  --curve-family x1-27 --x1-require-point4 1 --curve-threads 10 \
  --sea-level-telemetry 0 --schoof-fallback 1 --skip-incomplete-curves 0 \
  --smooth-coordinators 1 --table-dir data/modpoly/weber_f \
  --smooth-cache "${fake_root}/caches/test/smooth.cache" \
  --smooth-cache-sha256 "$cache_sha" --max-curves 1 --resume \
  >/dev/null 2>&1; then
  fail 'resume accepted a mutated command wrapper'
fi
mv "${tmp_dir}/command.sh.saved" "$command_path"

if PATH="${tmp_dir}/bin:${PATH}" python3 "${SCRIPT_DIR}/remote_worker.py" \
  --root "$fake_root" --instance-id "$instance" --run-id integration \
  --run-kind benchmark --prime 101 --worker-id 2 --worker-count 3 \
  --range-start 10 --range-end 21 --seed 7 --max-level 11 --sea-threads 4 \
  --curve-family x1-27 --x1-require-point4 1 --curve-threads 9 \
  --sea-level-telemetry 0 --schoof-fallback 1 --skip-incomplete-curves 0 \
  --smooth-coordinators 1 --table-dir data/modpoly/weber_f \
  --smooth-cache "${fake_root}/caches/test/smooth.cache" \
  --smooth-cache-sha256 "$cache_sha" --max-curves 1 --resume \
  >/dev/null 2>&1; then
  fail 'resume accepted changed semantic options'
fi

bash -n "${SCRIPT_DIR}"/*.sh
python3 -m py_compile "${SCRIPT_DIR}"/*.py
printf 'ok: AWS dry-run, cost tags, bounded launch, exact sharding, semantic argv, SSM polling, and immutable worker tests passed\n'
