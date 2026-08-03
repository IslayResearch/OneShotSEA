#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

instance_id=''
cache_id=''
prime=''
levels=''
maximum_prime_candidates=1000000
maximum_x_candidates=1000000
sea_threads=1
max_file_bytes=4294967296
expected_cache_sha256=''

usage() {
  cat <<'EOF'
Usage: prepare-direct-cache.sh --cache-id ID --prime P --levels L1,L2,...
       [--maximum-prime-candidates N] [--maximum-x-candidates N]
       [--sea-threads N] [--max-file-bytes N]
       [--expected-cache-sha256 SHA256] [--instance-id ID] [--execute]

Builds a curve-independent specialized classical-j context with the exact
deployed production binary. The immutable manifest binds the ordered level
schedule, construction/resource caps, target, deployed source/binary, command,
and cache digest. Dry-run is the default; existing cache directories are never
overwritten.
EOF
}

while (( $# )); do
  case "$1" in
    --instance-id) instance_id="${2:-}"; shift 2 ;;
    --cache-id) cache_id="${2:-}"; shift 2 ;;
    --prime) prime="${2:-}"; shift 2 ;;
    --levels) levels="${2:-}"; shift 2 ;;
    --maximum-prime-candidates) maximum_prime_candidates="${2:-}"; shift 2 ;;
    --maximum-x-candidates) maximum_x_candidates="${2:-}"; shift 2 ;;
    --sea-threads) sea_threads="${2:-}"; shift 2 ;;
    --max-file-bytes) max_file_bytes="${2:-}"; shift 2 ;;
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
validate_positive_uint maximum-prime-candidates "$maximum_prime_candidates"
validate_positive_uint maximum-x-candidates "$maximum_x_candidates"
validate_positive_uint sea-threads "$sea_threads"
validate_positive_uint max-file-bytes "$max_file_bytes"
(( 10#$max_file_bytes >= 96 )) || die 'max-file-bytes must be at least 96'
require_cmd python3
if ! python3 - "$levels" <<'PY'
import math
import re
import sys

text = sys.argv[1]
if not re.fullmatch(r"[1-9][0-9]*(?:,[1-9][0-9]*)*", text):
    raise SystemExit(1)
values = [int(value) for value in text.split(",")]
if len(values) != len(set(values)) or any(value > (1 << 32) - 1 for value in values):
    raise SystemExit(1)
for value in values:
    if value <= 3 or any(value % divisor == 0 for divisor in range(2, math.isqrt(value) + 1)):
        raise SystemExit(1)
PY
then
  die 'levels must be ordered distinct canonical decimal primes greater than three'
fi
if [[ -n "$expected_cache_sha256" ]]; then
  [[ "$expected_cache_sha256" =~ ^[0-9a-f]{64}$ ]] ||
    die '--expected-cache-sha256 must be a trusted lowercase SHA-256 digest'
fi
validate_remote_root

if ! require_execute; then
  printf 'DRY-RUN: on %s build %s/direct-caches/%s/direct.ctx with deployed production binary\n' \
    "$instance_id" "$AWS_REMOTE_ROOT" "$cache_id"
  printf 'DRY-RUN: bind prime=%s ordered_levels=%s max_prime_candidates=%s max_x_candidates=%s sea_threads=%s max_file_bytes=%s\n' \
    "$prime" "$levels" "$maximum_prime_candidates" "$maximum_x_candidates" \
    "$sea_threads" "$max_file_bytes"
  if [[ -n "$expected_cache_sha256" ]]; then
    printf 'DRY-RUN: require rebuilt direct cache SHA-256=%s\n' \
      "$expected_cache_sha256"
  fi
  exit 0
fi

ssm_require_online "$instance_id"
# shellcheck disable=SC2016
remote_bootstrap='set -euo pipefail
root=$1; instance_id=$2; cache_id=$3; prime=$4; levels=$5; max_primes=$6; max_x=$7; sea_threads=$8; max_file_bytes=$9; expected_cache_sha=${10}
deploy=$(readlink -f "$root/current")
[[ -d "$deploy" ]]
build_manifest="$deploy/build-manifest.json"
identity=$(python3 - "$build_manifest" "$instance_id" <<"PY"
import json, sys
import re
with open(sys.argv[1], encoding="utf-8") as stream:
    value = json.load(stream)
if value.get("schema") != "oneshotsea.aws-build.v1" or value.get("instance_id") != sys.argv[2]:
    raise SystemExit("error: invalid build manifest")
if not re.fullmatch(r"[0-9a-f]{40}", value.get("deployment_commit", "")):
    raise SystemExit("error: invalid deployment commit")
if not re.fullmatch(r"[0-9a-f]{64}", value.get("binary_sha256", "")):
    raise SystemExit("error: invalid deployed binary digest")
print(value["deployment_commit"], value["binary_sha256"])
PY
)
read -r commit binary_sha <<<"$identity"
exe="$deploy/build/oneshotsea"
[[ -x "$exe" ]]
[[ "$(sha256sum "$exe" | awk "{print \$1}")" == "$binary_sha" ]]
cache_dir="$root/direct-caches/$cache_id"
mkdir -p "$cache_dir"
test -z "$(find "$cache_dir" -mindepth 1 -maxdepth 1 -print -quit)"
cache="$cache_dir/direct.ctx"
cmd=("$exe" prepare-classical-direct-context --p "$prime"
     --classical-direct-levels "$levels"
     --classical-direct-max-prime-candidates "$max_primes"
     --classical-direct-max-x-candidates "$max_x"
     --classical-direct-context-max-file-bytes "$max_file_bytes"
     --sea-threads "$sea_threads" --output "$cache")
printf "exec" >"$cache_dir/command.sh"; printf " %q" "${cmd[@]}" >>"$cache_dir/command.sh"; printf "\n" >>"$cache_dir/command.sh"
"${cmd[@]}" >"$cache_dir/build.log" 2>&1
[[ -s "$cache" ]]
cache_sha=$(sha256sum "$cache" | awk "{print \$1}")
cache_bytes=$(wc -c <"$cache")
if [[ -n "$expected_cache_sha" && "$cache_sha" != "$expected_cache_sha" ]]; then
  printf "error: rebuilt direct cache digest %s does not match trusted digest %s\n" \
    "$cache_sha" "$expected_cache_sha" >&2
  exit 2
fi
python3 - "$cache_dir/build.log" "$prime" "$levels" "$max_primes" "$max_x" "$sea_threads" "$max_file_bytes" "$cache_sha" "$cache_bytes" <<"PY"
import json, sys
path, prime, levels, max_primes, max_x, threads, max_bytes, digest, cache_bytes = sys.argv[1:]
with open(path, encoding="utf-8") as stream:
    value = json.load(stream)
expected = {
    "schema": "oneshotsea.classical-direct-context.v1",
    "prime": prime,
    "levels": levels.split(","),
    "maximum_prime_candidates": max_primes,
    "maximum_x_candidates_per_surface": max_x,
    "thread_limit": threads,
    "max_file_bytes": max_bytes,
    "sha256": digest,
}
for key, wanted in expected.items():
    if value.get(key) != wanted:
        raise SystemExit(f"error: direct cache build output mismatch for {key}")
if int(value.get("context_count", -1)) != len(expected["levels"]):
    raise SystemExit("error: direct cache context count mismatch")
if int(value.get("file_bytes", -1)) != int(cache_bytes) or int(cache_bytes) > int(max_bytes):
    raise SystemExit("error: direct cache file size is outside the admission limit")
PY
python3 - "$cache_id" "$prime" "$levels" "$max_primes" "$max_x" \
  "$sea_threads" "$max_file_bytes" "$cache_bytes" "$commit" "$binary_sha" "$cache_sha" \
  "$expected_cache_sha" "${cmd[@]}" <<"PY" >"$cache_dir/manifest.json"
import json, sys
cache_id, prime, levels, max_primes, max_x, threads, max_bytes, cache_bytes, commit, binary_sha, cache_sha, expected = sys.argv[1:13]
json.dump({
    "schema": "oneshotsea.aws-direct-cache.v1", "cache_id": cache_id,
    "prime": prime, "ordered_levels": levels.split(","),
    "maximum_prime_candidates": int(max_primes),
    "maximum_x_candidates_per_surface": int(max_x),
    "sea_threads": int(threads), "max_file_bytes": int(max_bytes),
    "file_bytes": int(cache_bytes),
    "deployment_commit": commit, "binary_sha256": binary_sha,
    "direct_cache_sha256": cache_sha,
    "expected_direct_cache_sha256": expected or None,
    "command_argv": sys.argv[13:],
}, sys.stdout, sort_keys=True, separators=(",", ":"))
sys.stdout.write("\n")
PY
printf "direct_cache=%s\ndirect_cache_sha256=%s\n" "$cache" "$cache_sha"
'
printf -v remote_command 'bash -c %q -- %q %q %q %q %q %q %q %q %q %q' \
  "$remote_bootstrap" "$AWS_REMOTE_ROOT" "$instance_id" "$cache_id" \
  "$prime" "$levels" "$maximum_prime_candidates" "$maximum_x_candidates" \
  "$sea_threads" "$max_file_bytes" "$expected_cache_sha256"
ssm_run_command "$instance_id" 'OneShotSEA build direct cache' "$remote_command" 604800
