#!/usr/bin/env bash

# Shared helpers for the RunPod operator scripts.  This file is sourced.

set -euo pipefail

RUNPOD_API_BASE="${RUNPOD_API_BASE:-https://api.runpod.io}"
RUNPOD_STATE_DIR="${RUNPOD_STATE_DIR:-${XDG_STATE_HOME:-${HOME}/.local/state}/oneshotsea/runpod}"
RUNPOD_REMOTE_ROOT="${RUNPOD_REMOTE_ROOT:-/workspace/OneShotSEA}"
EXECUTE=0

die() {
  printf 'error: %s\n' "$*" >&2
  exit 2
}

note() {
  printf '%s\n' "$*" >&2
}

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

print_cmd() {
  local arg
  printf 'DRY-RUN:'
  for arg in "$@"; do
    printf ' %q' "$arg"
  done
  printf '\n'
}

validate_pod_id() {
  [[ "$1" =~ ^[A-Za-z0-9][A-Za-z0-9_-]{2,127}$ ]] || die "invalid pod id"
}

validate_run_id() {
  [[ "$1" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$ ]] || die "invalid run id"
}

validate_uint() {
  [[ "$2" =~ ^[0-9]+$ ]] || die "$1 must be an unsigned decimal integer"
}

validate_positive_uint() {
  validate_uint "$1" "$2"
  [[ "$2" != 0 ]] || die "$1 must be positive"
}

validate_remote_root() {
  if [[ -n "${RUNPOD_TEST_REMOTE_ROOT:-}" ]]; then
    [[ "${RUNPOD_SSH_HOST:-}" == '203.0.113.10' ]] ||
      die "test remote roots require the reserved local-test SSH host"
    [[ "$RUNPOD_REMOTE_ROOT" == "$RUNPOD_TEST_REMOTE_ROOT" ]] ||
      die "RUNPOD_REMOTE_ROOT must equal RUNPOD_TEST_REMOTE_ROOT in test mode"
    [[ "$RUNPOD_REMOTE_ROOT" =~ ^/[A-Za-z0-9._/+~-]+$ ]] ||
      die "test remote root must be a simple absolute path"
    [[ "$RUNPOD_REMOTE_ROOT" != *'/../'* && "$RUNPOD_REMOTE_ROOT" != */.. ]] ||
      die "test remote root may not contain parent traversal"
    return
  fi
  [[ "$RUNPOD_REMOTE_ROOT" =~ ^/workspace/[A-Za-z0-9._/-]+$ ]] ||
    die "RUNPOD_REMOTE_ROOT must be a simple absolute path below /workspace"
  [[ "$RUNPOD_REMOTE_ROOT" != *'/../'* && "$RUNPOD_REMOTE_ROOT" != */.. ]] ||
    die "RUNPOD_REMOTE_ROOT may not contain parent traversal"
}

require_execute() {
  if (( EXECUTE == 0 )); then
    note "dry-run only; pass --execute to perform this operation"
    return 1
  fi
  return 0
}

reject_xtrace() {
  [[ "$-" != *x* ]] || die "refusing to handle credentials while shell xtrace is enabled"
}

validate_api_base() {
  if [[ "$RUNPOD_API_BASE" != https://* ]]; then
    [[ "${RUNPOD_ALLOW_INSECURE_API:-0}" == 1 ]] ||
      die "RUNPOD_API_BASE must use HTTPS (set RUNPOD_ALLOW_INSECURE_API=1 only for a local test server)"
  fi
  [[ "$RUNPOD_API_BASE" != *$'\n'* && "$RUNPOD_API_BASE" != *$'\r'* ]] ||
    die "invalid RUNPOD_API_BASE"
}

# Usage: api_request METHOD PATH [JSON_BODY]
# The API key is supplied to curl over stdin as a config directive, so it is
# neither a command-line argument nor written to disk.  Callers must never use
# this function under `set -x`.
api_request() {
  local method="$1"
  local path="$2"
  local body="${3:-}"
  local body_file=''
  local rc=0
  local allowed_path_re='^/v2/[A-Za-z0-9_/?&=.,%+-]+$'

  require_cmd curl
  validate_api_base
  reject_xtrace
  [[ "$path" =~ $allowed_path_re ]] || die "invalid RunPod API path"
  [[ "${RUNPOD_API_KEY:-}" =~ ^[A-Za-z0-9._-]+$ ]] ||
    die "RUNPOD_API_KEY is missing or contains unsupported characters"

  if [[ -n "$body" ]]; then
    body_file="$(mktemp "${TMPDIR:-/tmp}/oneshotsea-runpod-body.XXXXXX")"
    chmod 600 "$body_file"
    printf '%s' "$body" >"$body_file"
  fi

  # Avoid a broad or unresolved deletion target in cleanup.
  cleanup_api_body() {
    if [[ -n "$body_file" && -f "$body_file" &&
          "$body_file" == "${TMPDIR:-/tmp}"/oneshotsea-runpod-body.* ]]; then
      rm -f -- "$body_file"
    fi
  }
  trap cleanup_api_body RETURN

  local curl_args=(
    --silent --show-error --fail-with-body
    --request "$method"
    --header 'Accept: application/json'
    --url "${RUNPOD_API_BASE}${path}"
  )
  if [[ -n "$body_file" ]]; then
    curl_args+=(
      --header 'Content-Type: application/json'
      --data-binary "@${body_file}"
    )
  fi

  # `printf` is a shell builtin. The bearer token therefore does not appear in
  # the process table. curl reads its Authorization header from stdin.
  printf 'header = "Authorization: Bearer %s"\n' "$RUNPOD_API_KEY" |
    curl --disable --config - "${curl_args[@]}" || rc=$?
  cleanup_api_body
  trap - RETURN
  return "$rc"
}

pod_state_path() {
  local pod_id="$1"
  validate_pod_id "$pod_id"
  printf '%s/pods/%s.json\n' "$RUNPOD_STATE_DIR" "$pod_id"
}

# Persist only an allowlisted, non-secret subset of a Pod response. In
# particular, API-returned env/args fields are never written to operator state.
save_pod_state() {
  local response="$1"
  local pod_id state_path state_tmp old_rate='null' old_uptime='null' old_started='null'
  require_cmd jq
  pod_id="$(jq -er '.id | strings' <<<"$response")" || die "pod response has no id"
  validate_pod_id "$pod_id"
  state_path="$(pod_state_path "$pod_id")"
  umask 077
  mkdir -p "$(dirname "$state_path")"
  chmod 700 "$(dirname "$state_path")"
  if [[ -f "$state_path" ]]; then
    old_rate="$(jq -r '.provisioned_hourly_rate_usd // null' "$state_path")"
    old_uptime="$(jq -r '.runtime_uptime_seconds // null' "$state_path")"
    old_started="$(jq -r '.started_at // null' "$state_path")"
  fi
  state_tmp="${state_path}.tmp.$$"
  jq --arg checked_at "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
     --argjson old_rate "$old_rate" \
     --argjson old_uptime "$old_uptime" \
     --argjson old_started "$old_started" '
    {
      schema: 1,
      pod_id: .id,
      name: .name,
      image: .image,
      status: .status,
      actions: (.actions // []),
      gpu: (.gpu // null),
      cloud: .cloud,
      data_center_id: (.dataCenterId // null),
      current_hourly_rate_usd: (.cost // null),
      provisioned_hourly_rate_usd:
        (if ((.cost // 0) > 0) then .cost else $old_rate end),
      created_at: .createdAt,
      started_at: (.startedAt // $old_started),
      runtime_uptime_seconds: (.runtime.uptime // $old_uptime),
      runtime_ports: (.runtime.ports // []),
      last_checked_at: $checked_at
    }' <<<"$response" >"$state_tmp"
  mv -f -- "$state_tmp" "$state_path"
  printf '%s\n' "$state_path"
}

load_pod_id() {
  local explicit="${1:-}"
  local pod_id="$explicit"
  if [[ -z "$pod_id" ]]; then
    pod_id="${RUNPOD_POD_ID:-}"
  fi
  [[ -n "$pod_id" ]] || die "provide --pod-id or set RUNPOD_POD_ID"
  validate_pod_id "$pod_id"
  printf '%s\n' "$pod_id"
}

build_ssh_args() {
  local host="${RUNPOD_SSH_HOST:-}"
  local port="${RUNPOD_SSH_PORT:-22}"
  local user="${RUNPOD_SSH_USER:-root}"

  [[ "$host" =~ ^[A-Za-z0-9.-]+$ ]] ||
    die "RUNPOD_SSH_HOST must be a DNS name or IPv4 address"
  validate_positive_uint RUNPOD_SSH_PORT "$port"
  (( 10#$port <= 65535 )) || die "RUNPOD_SSH_PORT is out of range"
  [[ "$user" =~ ^[A-Za-z_][A-Za-z0-9_-]*$ ]] || die "invalid RUNPOD_SSH_USER"

  # Assigned for callers after this sourced helper returns.
  # shellcheck disable=SC2034
  SSH_DESTINATION="${user}@${host}"
  SSH_ARGS=(
    -p "$port"
    -o BatchMode=yes
    -o IdentitiesOnly=yes
    -o StrictHostKeyChecking=accept-new
    -o ConnectTimeout=15
    -o ServerAliveInterval=30
    -o ServerAliveCountMax=3
  )
  if [[ -n "${RUNPOD_SSH_KEY_FILE:-}" ]]; then
    [[ -f "$RUNPOD_SSH_KEY_FILE" ]] || die "RUNPOD_SSH_KEY_FILE does not exist"
    SSH_ARGS+=( -i "$RUNPOD_SSH_KEY_FILE" )
  fi
  if [[ -n "${RUNPOD_SSH_KNOWN_HOSTS:-}" ]]; then
    SSH_ARGS+=( -o "UserKnownHostsFile=${RUNPOD_SSH_KNOWN_HOSTS}" )
  fi
}
