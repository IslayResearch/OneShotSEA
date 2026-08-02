# RunPod operations

These scripts implement the stateful-pod workflow used for the p26 GPU run:
deploy a minimal source snapshot to `/workspace`, build with the pod's CUDA
toolchain, run under `tmux`, retain compact logs/checkpoints, fetch all evidence,
and stop compute promptly. They use the official RunPod API v2 endpoints at
`https://api.runpod.io/v2`.

No script contacts RunPod, opens SSH, changes a pod, or writes operator state
unless `--execute` is present. Run every command once without `--execute` and
review the plan before enabling it.

## Safety and credentials

- Put the API key only in `RUNPOD_API_KEY`. The scripts never accept it as an
  argument, write it to disk, or print it. The HTTP helper refuses to run under
  shell xtrace and supplies the bearer header to `curl` over stdin.
- Put the SSH private-key *path* in `RUNPOD_SSH_KEY_FILE`; do not copy the key
  into this repository or `/workspace`.
- Do not put secrets in `--build-command`, source files, search
  logs, or checkpoints. RunPod pod environment variables are deliberately not
  used by this workflow.
- Deployment uses `git archive HEAD`, requires a clean worktree, and transfers
  only committed files. `.git`, untracked files, shell environment, and local
  credential files cannot enter the archive.
- API responses are reduced to an allowlisted state record; returned `env` and
  `args` values are never persisted. State defaults to
  `${XDG_STATE_HOME:-$HOME/.local/state}/oneshotsea/runpod`, outside the repo.
- SSH uses `BatchMode`, `IdentitiesOnly`, and `StrictHostKeyChecking=accept-new`.
  Confirm the first host fingerprint against the RunPod console. Set
  `RUNPOD_SSH_KNOWN_HOSTS` to use a dedicated known-hosts file.
- `stop.sh` releases compute but preserves the pod disk. There is intentionally
  no termination helper because termination permanently deletes the pod.

Load a key without placing its value in shell history:

```bash
read -r -s -p 'RunPod API key: ' RUNPOD_API_KEY; printf '\n'
export RUNPOD_API_KEY
```

Run the local contract tests before cloud work:

```bash
make test-runpod
```

## 1. Inspect inventory and provision

The catalog request includes current pod availability and price. Compare a
small benchmark across plausible GPUs before choosing production workers; Ada
RTX 4090 and RTX 6000 Ada are known-good operational precedents, not assumed
winners for 416-bit multi-limb arithmetic.

```bash
scripts/runpod/catalog.sh --cloud SECURE --gpu-count 1 --gpu '4090|6000 Ada'
scripts/runpod/catalog.sh --cloud SECURE --gpu-count 1 --gpu '4090|6000 Ada' --execute
```

Provisioning requires an explicit hourly ceiling. It creates a stateful
host-local `/workspace` mount and exposes only SSH. The script checks the
catalog before creation and stops the new pod immediately if the returned rate
exceeds the ceiling. Host-local persistent storage survives stop/start but not
a host failure, so fetch checkpoints regularly.

```bash
scripts/runpod/provision.sh \
  --name oneshotsea-bench-a \
  --gpu-id 'NVIDIA GeForce RTX 4090' \
  --cloud SECURE \
  --disk-gb 50 \
  --volume-gb 100 \
  --max-price-per-hour 1.00

scripts/runpod/provision.sh \
  --name oneshotsea-bench-a \
  --gpu-id 'NVIDIA GeForce RTX 4090' \
  --cloud SECURE \
  --disk-gb 50 \
  --volume-gb 100 \
  --max-price-per-hour 1.00 \
  --execute
```

Record the returned pod id:

```bash
export RUNPOD_POD_ID='pod-id-from-the-response'
```

Provisioning is asynchronous. Poll until `status` is `RUNNING`; do not infer
readiness from the create response:

```bash
scripts/runpod/status.sh --execute
```

The status response's `runtime.ports` entry with private port `22` gives the
public SSH IP and port. Set them without putting key material in the repo:

```bash
export RUNPOD_SSH_HOST='public-ip-from-runtime-ports'
export RUNPOD_SSH_PORT='public-port-mapped-to-22'
export RUNPOD_SSH_USER=root
export RUNPOD_SSH_KEY_FILE='/absolute/path/to/private/key'
```

For an already-provisioned stopped pod, review and then start it:

```bash
scripts/runpod/start.sh
scripts/runpod/start.sh --execute
```

## 2. Commit, deploy, and build natively

Commit and test each coherent code increment before deployment. `deploy.sh`
refuses a dirty worktree and installs the exact commit into
`/workspace/OneShotSEA/deployments/<commit>`. The `current` symlink changes only
after the optional build succeeds. It also records the available GPU and build
tools, kernel, and commit information in `environment.txt`.

Review the actual GPU compute capability and choose the matching CUDA
architecture. For the Ada precedent this was `89`; do not copy it to a
different GPU blindly.

```bash
scripts/runpod/deploy.sh \
  --build-command 'make -j"$(nproc)" all'

scripts/runpod/deploy.sh \
  --build-command 'make -j"$(nproc)" all' \
  --execute
```

Build commands and arguments must be non-secret. The p26 command line and
96-bit backend are operational examples only; they are not suitable arithmetic
for this 416–432-bit SEA search.

## 3. Allocate deterministic ranges

Every worker receives the same global half-open curve-index range and seed.
`oneshotsea search` partitions that range internally using the worker id and
count. Do not pre-shard its `--range-start` and `--range-end`: doing so would
partition the interval twice and silently leave most of it unsearched.

`shard.py` reports both the shared CLI values and the subrange that the CLI will
assign to one worker. Values are checked against the executable's unsigned
64-bit range. Any remainder goes to the lowest worker ids.

```bash
scripts/runpod/shard.py \
  --global-start 0 \
  --global-count 1000000000 \
  --worker-id 0 \
  --worker-count 4 \
  --seed 202607290000
```

The JSON fields `range_start`, `range_end`, `range_count`, and `seed` are
identical for all workers. The `assigned_range_*` fields are for reporting and
coverage audits only; do not pass them back as the global range. Generate and
retain one record per worker. Assign each zero-based worker id exactly once. A
changed worker count defines a different partition and must use a new run id or
an explicitly unsearched global interval.

The search executable launched by this scaffold must accept this operator
contract:

```text
search --p P
--worker-id I --worker-count W
--range-start FIRST --range-end EXCLUSIVE
--seed SEED
--max-level L
--sea-threads N
--table-dir PATH
--smooth-cache PATH --smooth-cache-sha256 TRUSTED_SHA256
--checkpoint PATH
--progress PATH
--certificate-out PATH
```

`--range-start` must completely determine the first curve candidate and each
subsequent integer index must determine exactly one candidate. A seed may
choose a deterministic permutation or family, but must not turn the range into
an untracked random stream.

## 4. Prepare the smooth cache

All workers for a target should share one prebuilt smooth cache. The launcher
requires its trusted SHA-256 so an existing, incomplete, or substituted cache
cannot be accepted silently. One way to build a new cache without searching a
curve is the production executable itself:

```bash
SMOOTH_CACHE='/workspace/OneShotSEA/caches/p125.cache'

mkdir -p /workspace/OneShotSEA/caches /workspace/OneShotSEA/cache-build/p125
/workspace/OneShotSEA/current/build/oneshotsea search \
  --p 100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237 \
  --seed 202607290000 \
  --range-start 0 --range-end 1 \
  --worker-id 0 --worker-count 1 \
  --max-level 401 \
  --table-dir /workspace/OneShotSEA/current/data/modpoly/weber_f \
  --smooth-cache "$SMOOTH_CACHE" \
  --checkpoint /workspace/OneShotSEA/cache-build/p125/checkpoint.json \
  --progress /workspace/OneShotSEA/cache-build/p125/progress.jsonl \
  --certificate-out /workspace/OneShotSEA/cache-build/p125/certificate.txt \
  --max-curves 0
sha256sum "$SMOOTH_CACHE"
```

Retain the digest with the build record and independently check that the build
completed with the intended target and executable. A digest calculated from an
unknown cache proves only byte identity, not mathematical completeness.

## 5. Launch and supervise workers

Use a stable run id. For one shard, transfer the JSON values directly:

```bash
RUN_ID='p125-bench-20260729'
SMOOTH_CACHE='/workspace/OneShotSEA/caches/p125.cache'
SMOOTH_CACHE_SHA256='trusted-64-lowercase-hex-digest-from-build-record'

scripts/runpod/launch-worker.sh \
  --run-id "$RUN_ID" \
  --run-kind production \
  --prime 100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237 \
  --worker-id 0 --worker-count 1 \
  --range-start 0 --range-end 1000000000 \
  --seed 202607290000 \
  --max-level 401 \
  --curve-family x1-27 --x1-require-point4 1 \
  --curve-threads 16 --sea-threads 1 --sea-level-telemetry 0 \
  --schoof-fallback 1 --skip-incomplete-curves 0 \
  --trace-cap 16 --smooth-threads 1 --smooth-coordinators 0 \
  --wall-time-limit-seconds 14400 \
  --table-dir data/modpoly/weber_f \
  --smooth-cache "$SMOOTH_CACHE" \
  --smooth-cache-sha256 "$SMOOTH_CACHE_SHA256"

scripts/runpod/launch-worker.sh \
  --run-id "$RUN_ID" \
  --run-kind production \
  --prime 100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237 \
  --worker-id 0 --worker-count 1 \
  --range-start 0 --range-end 1000000000 \
  --seed 202607290000 \
  --max-level 401 \
  --curve-family x1-27 --x1-require-point4 1 \
  --curve-threads 16 --sea-threads 1 --sea-level-telemetry 0 \
  --schoof-fallback 1 --skip-incomplete-curves 0 \
  --trace-cap 16 --smooth-threads 1 --smooth-coordinators 0 \
  --wall-time-limit-seconds 14400 \
  --table-dir data/modpoly/weber_f \
  --smooth-cache "$SMOOTH_CACHE" \
  --smooth-cache-sha256 "$SMOOTH_CACHE_SHA256" \
  --execute
```

For several pods, set `--worker-count` to the total pod count and launch every
worker id exactly once on its assigned pod, changing no other search-identity
value. Do not launch several 16-curve workers on one 16-vCPU pod.
The launcher accepts only explicit search and resource controls, including the
curve family and point-four admission gate, curve/SEA/smooth topology, fallback
policy, telemetry, trace cap, and wall-time bound. It has no generic argument
passthrough that could override worker identity or durable output paths.

Each worker gets a `tmux` session named `sea-<run-id>-<worker-id>` and this
state below `/workspace/OneShotSEA/runs/<run-id>/worker-<id>/`:

```text
manifest.json       immutable command argv, ranges, seed, prime, commit/binary, start
command.sh          exact shell-escaped search command (mode 0700)
worker.log          stdout/stderr
resource-usage.txt  append-only, delimited GNU-time evidence for every attempt
attempts.jsonl      append-only start/end timestamps and exit statuses
progress.jsonl      compact per-chunk counters and timings
checkpoint.json     atomic resume state
certificate.txt     candidate certificate, if found
```

The search program should write checkpoints through a temporary file plus
atomic rename. A checkpoint should include a schema version, prime, seed,
worker/range identity, deployed commit, next unsearched index, and any state
needed to reproduce continuation. Reject mismatched checkpoints. Log one
compact progress record per substantial chunk rather than per curve.

Inspect API, GPU utilization, `tmux`, and the last two progress records:

```bash
scripts/runpod/status.sh --remote --run-id "$RUN_ID"
scripts/runpod/status.sh --remote --run-id "$RUN_ID" --execute
```

Use `--resume` with the identical launcher command after an interruption. The
search CLI resumes automatically through the same `--checkpoint` path. Before
launching, the wrapper compares the complete search argv and deployed commit to
`manifest.json`; resource, identity, cache, deployment, or output changes are
rejected.

## 6. Fetch, verify, and stop

Fetch periodically and immediately when a worker finishes or emits a
candidate. The transfer is additive: it never uses `--delete` and never removes
remote evidence.

```bash
scripts/runpod/fetch.sh --run-id "$RUN_ID"
scripts/runpod/fetch.sh --run-id "$RUN_ID" --execute
```

The default local destination is
`artifacts/runpod/<run-id>/<pod-id>/`. Fetch adds `SHA256SUMS` and an
allowlisted `fetch-metadata.json`. Verify any certificate locally with the
unmodified canonical `voneshot.py` before treating it as a result. Cloud logs or
the worker's own success status are not independent verification.

After artifacts are safe locally—or immediately when the pod is no longer
cost-effective—review and stop it:

```bash
scripts/runpod/stop.sh
scripts/runpod/stop.sh --execute
```

Confirm `EXITED` and zero current compute rate:

```bash
scripts/runpod/status.sh --execute
```

If funds are exhausted, stop cloud work and continue local implementation and
analysis. Never loop on provisioning failures or silently raise the price cap.

## Cost and benchmark records

The local pod state retains GPU type, cloud/data center, quoted running rate,
created/start times, last observed uptime, and port metadata. Fetch metadata
adds a clearly labeled `uptime × hourly rate` session estimate; it is not an
invoice. Use RunPod billing as the authoritative charge record.

For every benchmark or production run, retain:

- pod id, GPU name/count/UUID, driver and CUDA/compiler versions;
- hourly rate, pod lifetime, effective search time, and authoritative or
  explicitly estimated cost;
- deployed commit, target prime, exact half-open range, worker count/id, seed,
  batch/kernel parameters, and checkpoint;
- attempted curves and rejection counts by stage, completed counts,
  throughput, major-kernel/SEA-prime timing, and peak host/device memory; and
- logs, candidate output, hashes, and the independent local verifier transcript.

This makes a no-hit range useful evidence and prevents overlapping cloud work
from being mistaken for additional coverage.

## Sources

- [P26 GPU throughput report, Cloud Operations](https://github.com/alexamclain/Danger2026DataChallenge/blob/main/research/p26/gpu-throughput-report_20260620.md)
- [RunPod API v2 overview](https://docs.runpod.io/api-reference-v2/overview)
- [Create a pod](https://docs.runpod.io/api-reference-v2/pods/create-a-pod)
- [Trigger a pod state transition](https://docs.runpod.io/api-reference-v2/pods/trigger-a-pod-state-transition)
