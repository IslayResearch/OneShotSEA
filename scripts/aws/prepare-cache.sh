#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

instance_id=''
cache_id=''
prime=''
max_level=''
table_dir=''
seed=0
expected_cache_sha256=''

usage() {
  cat <<'EOF'
Usage: prepare-cache.sh --cache-id ID --prime P --max-level L
       --table-dir RELPATH [--seed N] [--expected-cache-sha256 SHA256]
       [--instance-id ID] [--execute]

Builds a smooth cache with the exact deployed production binary and writes a
cache manifest binding its digest, commit, binary, target, and command. Dry-run
is the default. Existing cache directories are never overwritten.
EOF
}

while (( $# )); do
  case "$1" in
    --instance-id) instance_id="${2:-}"; shift 2 ;;
    --cache-id) cache_id="${2:-}"; shift 2 ;;
    --prime) prime="${2:-}"; shift 2 ;;
    --max-level) max_level="${2:-}"; shift 2 ;;
    --table-dir) table_dir="${2:-}"; shift 2 ;;
    --seed) seed="${2:-}"; shift 2 ;;
    --expected-cache-sha256) expected_cache_sha256="${2:-}"; shift 2 ;;
    --execute) EXECUTE=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown argument: $1" ;;
  esac
done

instance_id="$(load_instance_id "$instance_id")"
[[ -n "$cache_id" ]] || die '--cache-id is required'
validate_run_id "$cache_id"
validate_positive_uint prime "$prime"
validate_positive_uint max-level "$max_level"
(( 10#$max_level >= 5 )) || die 'max-level must be at least 5'
validate_uint seed "$seed"
if [[ -n "$expected_cache_sha256" ]]; then
  [[ "$expected_cache_sha256" =~ ^[0-9a-f]{64}$ ]] ||
    die '--expected-cache-sha256 must be a trusted lowercase SHA-256 digest'
fi
[[ "$table_dir" =~ ^[A-Za-z0-9._/+~-]+$ && "$table_dir" != /* ]] ||
  die 'table directory must be a simple relative path'
[[ "$table_dir" != ../* && "$table_dir" != */../* && "$table_dir" != */.. ]] ||
  die 'table directory may not traverse parents'
validate_remote_root

if ! require_execute; then
  printf 'DRY-RUN: on %s build %s/caches/%s/smooth.cache with deployed production binary\n' \
    "$instance_id" "$AWS_REMOTE_ROOT" "$cache_id"
  printf 'DRY-RUN: bind prime=%s seed=%s max_level=%s table_dir=%s and binary/commit SHA-256 identity\n' \
    "$prime" "$seed" "$max_level" "$table_dir"
  printf 'DRY-RUN: cache-only partition worker=1/2 assigned_range=[1,1) (zero curves)\n'
  if [[ -n "$expected_cache_sha256" ]]; then
    printf 'DRY-RUN: require rebuilt smooth cache SHA-256=%s\n' "$expected_cache_sha256"
  fi
  exit 0
fi

ssm_require_online "$instance_id"
# shellcheck disable=SC2016
remote_bootstrap='set -euo pipefail
root=$1; instance_id=$2; cache_id=$3; prime=$4; seed=$5; max_level=$6; table_dir=$7; expected_cache_sha=$8
deploy=$(readlink -f "$root/current")
[[ -d "$deploy" ]]
build_manifest="$deploy/build-manifest.json"
readarray -t identity < <(python3 - "$build_manifest" "$instance_id" <<"PY"
import json, sys
with open(sys.argv[1], encoding="utf-8") as stream:
    value = json.load(stream)
if value.get("schema") != "oneshotsea.aws-build.v1" or value.get("instance_id") != sys.argv[2]:
    raise SystemExit("error: invalid build manifest")
print(value["deployment_commit"])
print(value["binary_sha256"])
PY
)
commit=${identity[0]}; binary_sha=${identity[1]}
exe="$deploy/build/oneshotsea"; tables="$deploy/$table_dir"
[[ -x "$exe" && -d "$tables" ]]
[[ "$(sha256sum "$exe" | awk "{print \$1}")" == "$binary_sha" ]]
cache_dir="$root/caches/$cache_id"
mkdir -p "$cache_dir"
test -z "$(find "$cache_dir" -mindepth 1 -maxdepth 1 -print -quit)"
cache="$cache_dir/smooth.cache"
checkpoint="$cache_dir/checkpoint.json"
progress="$cache_dir/progress.jsonl"
result="$cache_dir/certificate.txt"
build_id="git:${commit}-sha256:${binary_sha}"
cmd=("$exe" search --p "$prime" --seed "$seed" --range-start 0 --range-end 1
     --worker-id 1 --worker-count 2 --max-level "$max_level" --table-dir "$tables"
     --smooth-cache "$cache" --checkpoint "$checkpoint" --progress "$progress"
     --certificate-out "$result" --build-id "$build_id" --max-curves 0)
printf "exec" >"$cache_dir/command.sh"; printf " %q" "${cmd[@]}" >>"$cache_dir/command.sh"; printf "\n" >>"$cache_dir/command.sh"
set +e
"${cmd[@]}" >"$cache_dir/build.log" 2>&1
status=$?
set -e
# Worker one of two receives the empty assigned range [1,1). Clean exhaustion
# returns zero after the CLI has authenticated/built the cache and emitted its
# summary. Any progress record or certificate would contradict the cache-only
# partition and is an operational failure.
[[ "$status" == 0 ]]
[[ ! -s "$progress" ]]
[[ ! -e "$result" ]]
[[ -s "$cache" ]]
cache_sha=$(sha256sum "$cache" | awk "{print \$1}")
if [[ -n "$expected_cache_sha" && "$cache_sha" != "$expected_cache_sha" ]]; then
  printf "error: rebuilt cache digest %s does not match trusted digest %s\n" \
    "$cache_sha" "$expected_cache_sha" >&2
  exit 2
fi
python3 - "$cache_id" "$prime" "$seed" "$max_level" "$table_dir" "$commit" \
  "$binary_sha" "$cache_sha" "$expected_cache_sha" "${cmd[@]}" <<"PY" >"$cache_dir/manifest.json"
import json, sys
cache_id, prime, seed, max_level, table_dir, commit, binary_sha, cache_sha, expected = sys.argv[1:10]
json.dump({
    "schema": "oneshotsea.aws-cache.v1", "cache_id": cache_id,
    "prime": prime, "seed": seed, "max_level": int(max_level),
    "table_dir": table_dir, "deployment_commit": commit,
    "binary_sha256": binary_sha, "smooth_cache_sha256": cache_sha,
    "expected_smooth_cache_sha256": expected or None,
    "command_argv": sys.argv[10:],
}, sys.stdout, sort_keys=True, separators=(",", ":"))
sys.stdout.write("\n")
PY
printf "smooth_cache=%s\nsmooth_cache_sha256=%s\n" "$cache" "$cache_sha"
'
printf -v remote_command 'bash -c %q -- %q %q %q %q %q %q %q %q' \
  "$remote_bootstrap" "$AWS_REMOTE_ROOT" "$instance_id" "$cache_id" \
  "$prime" "$seed" "$max_level" "$table_dir" "$expected_cache_sha256"
ssm_run_command "$instance_id" 'OneShotSEA build smooth cache' "$remote_command" 604800
