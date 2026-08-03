# AWS CPU operations

The AWS path runs CPU-only SEA benchmarks and production workers on stateful
EC2 instances.  Every mutating command is dry-run by default.  The scripts use
the ordinary AWS credential chain and never accept, print, or persist access
keys.  Instance control is keyless through Systems Manager; the worker security
group must have zero ingress rules.

## Cost and safety contract

Every instance and root EBS volume is tagged `Project=OneShotSEA` plus an
immutable `LaunchId`.  The `Project` user-defined cost-allocation tag was
activated on 2026-08-01.  Cost Explorer can lag live usage, so the retained
instance/fetch metadata also estimates a conservative launch-to-fetch cost
from the price authenticated immediately before launch.

Use `provision.sh --task-tag p125-coordinator-ab` to add the narrower
`Task=p125-coordinator-ab` tag to both the instance and root volume while
retaining `Project` and `LaunchId`. AWS Billing must separately activate the
`Task` user-defined cost-allocation tag before Cost Explorer can group or
filter charges by it; activation is not retroactive. `Project` remains the
already-activated fallback for total OneShotSEA spend.

Query tagged charges for an exclusive date range with:

```sh
scripts/aws/cost.sh --start 2026-08-01 --end 2026-08-02
scripts/aws/cost.sh --start 2026-08-01 --end 2026-08-02 --execute
scripts/aws/cost.sh --start 2026-08-01 --end 2026-08-02 \
  --task p125-production-shard --execute
```

Provisioning requires both a current on-demand price ceiling and a guest-side
lifetime backstop. Cloud-init arms a shutdown timer, instance-initiated
shutdown terminates the instance, and the root disk is encrypted and
delete-on-termination. The transient guest timer does not survive a reboot or
an unresponsive guest, so it is not an AWS-side spending guarantee: keep an
independent operator monitor, bound the worker well below the instance
lifetime, fetch with margin, and terminate promptly.

On 2026-08-01 the authenticated Ohio prices were:

| Type | vCPU | RAM | Architecture | On-demand price |
|---|---:|---:|---|---:|
| `m8g.xlarge` | 4 | 16 GiB | Graviton4/arm64 | $0.17952/hour |
| `c8g.4xlarge` | 16 | 32 GiB | Graviton4/arm64 | $0.63584/hour |
| `c8i.4xlarge` | 16 | 32 GiB | x86-64 | $0.74968/hour |

The account initially allowed five Standard-instance vCPUs.  Quota request
`c43139af0a154231a5de981e3eabcf65GeD2auPl` asks for 64; until approved,
`m8g.xlarge` is the bounded fallback.  A 30-minute fallback trial has at most
$0.08976 of instance charge at the recorded rate, plus EBS and data-transfer
charges.  Price is rechecked at execution time.

The completed fallback trial is recorded in `docs/aws_benchmark_20260801.md`.
Its four-thread level-193 pass took 179.20 seconds versus 87.813 seconds of
accounted SEA time for the same 42-level slice on the local M4 with ten
threads.  Do not use `m8g.xlarge` as a latency replacement for local cores;
use it only for additive shards or cloud-path validation.

## Current Ohio prerequisites

The research instance profile is
`expMath-ResearchInstanceProfile-JpspoidArRiA` and includes
`AmazonSSMManagedInstanceCore`.  The dedicated group
`sg-0852d5dccfde431c2` (`oneshotsea-ssm-egress-only`) has no ingress and the
default outbound rule.  The official Amazon Linux 2023 images used on
2026-08-01 were:

- arm64: `ami-0263206814db4826a`
- x86-64: `ami-06dd88604c99ec11f`

AMI ownership, architecture, instance metadata, security-group ingress, and
current price are all revalidated before launch.  The default subnet needs an
explicit public IP solely for outbound package/Git/SSM traffic; no inbound port
is opened.

## Bounded fallback trial

Dry-run first:

```sh
scripts/aws/provision.sh \
  --launch-id p125-m8g-trial-20260801 \
  --instance-type m8g.xlarge \
  --ami-id ami-0263206814db4826a \
  --subnet-id subnet-0800205b8fbcd6777 \
  --security-group-id sg-0852d5dccfde431c2 \
  --iam-instance-profile expMath-ResearchInstanceProfile-JpspoidArRiA \
  --task-tag p125-coordinator-ab \
  --associate-public-ip \
  --volume-gb 100 --max-price-per-hour 0.20 \
  --max-lifetime-minutes 30
```

Add `--execute` only after reviewing that output.  Export the returned ID as
`AWS_INSTANCE_ID`, wait for Systems Manager to report online, deploy a clean,
pushed commit, and build natively:

```sh
scripts/aws/status.sh --execute
scripts/aws/deploy.sh --jobs 4
scripts/aws/deploy.sh --jobs 4 --execute
```

`deploy.sh` fetches the exact local `HEAD` from the public canonical GitHub
remote, so deployment deliberately fails when the worktree is dirty or the
commit has not been pushed.

The production wrapper forwards the search semantics and resource topology
needed by the p125 X1(27) coordinator comparison. For example, add these flags
to an otherwise complete, bounded `launch-worker.sh` invocation:

```sh
  --curve-family x1-27 --x1-require-point4 1 \
  --trace-cap 16 --schoof-fallback 1 --skip-incomplete-curves 0 \
  --curve-threads 10 --sea-threads 1 --sea-level-telemetry 0 \
  --smooth-coordinators 1 --smooth-threads 8
```

`curve-family` is restricted to the CLI's `weber-f`, `x1-11`, and `x1-27`
allowlist. Boolean flags accept only `0` or `1`; curve threads must be positive;
smooth coordinators must be nonnegative and cannot exceed curve threads. The
remote worker writes every explicit value into `command_argv` in its immutable
manifest. A resume must repeat the same values: changing a semantic or resource
option fails closed with a manifest mismatch.

Run a CAS-free thread-scaling benchmark against one explicitly recorded target
curve with `benchmark-sea.sh`.  The fetched run contains the exact curve and
command, per-thread JSONL and GNU-time records, build/runtime environments,
commit and binary identities, and SHA-256 digests.

The full production worker additionally needs the authenticated `p125` smooth
cache under `/opt/oneshotsea/caches`.  `prepare-cache.sh` can reproduce it on
the instance and records a cache manifest; a short thread-scaling trial should
instead use the CAS-free SEA CLI directly so a 30-minute instance is not spent
rebuilding the 5.4 GB cache.

For the canonical p125 cache, pass
`--expected-cache-sha256 afe0927dd21aa1555c4b24ecab60636aedf4657c455a4d01ce0e65d863abf551`.
Production launch fails unless the cache manifest, deployed commit/binary, and
that independently trusted digest all agree. Each worker copies the build
manifest/environment/log, cache manifest/command/log, and launcher into its
run tree so `fetch.sh` retains the provenance before termination. Give the
worker an explicit wall-time limit that leaves ample deployment, fetch, and
verification margin before the guest lifetime backstop.

## Authenticated direct-first workers

The specialized classical-`j` context is a second, curve-independent cache.
Unlike the smooth cache, its identity includes the ordered level schedule,
both direct-construction caps, its preparation/search `--sea-threads` value,
and its whole-file admission limit. Build it on each deployed instance with
the exact production binary. For the measured p125 selected-20 prefix with a
deferred 89/97 cap-one suffix:

```sh
P=100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237
LEVELS=7,5,11,13,19,17,23,29,31,37,41,43,47,53,67,71,79,61,73,59,89,97

scripts/aws/prepare-direct-cache.sh \
  --cache-id p125-selected20-tail89-97 --prime "$P" --levels "$LEVELS" \
  --maximum-prime-candidates 10000000 \
  --maximum-x-candidates 1000000 --sea-threads 1 \
  --max-file-bytes 1000000000 \
  --expected-cache-sha256 \
    a459dc7732e0a8924f3dcce15bd640c5a72f594d403bf117d0fa45c6b3805625
scripts/aws/prepare-direct-cache.sh \
  --cache-id p125-selected20-tail89-97 --prime "$P" --levels "$LEVELS" \
  --maximum-prime-candidates 10000000 \
  --maximum-x-candidates 1000000 --sea-threads 1 \
  --max-file-bytes 1000000000 \
  --expected-cache-sha256 \
    a459dc7732e0a8924f3dcce15bd640c5a72f594d403bf117d0fa45c6b3805625 \
  --execute
```

Then add the following to the otherwise complete bounded worker invocation:

```sh
  --sea-strategy direct-first \
  --classical-direct-levels "$LEVELS" \
  --classical-direct-cap-one-tail-count 2 \
  --classical-direct-pre-smooth-tail-min-traces 2 \
  --classical-direct-max-prime-candidates 10000000 \
  --classical-direct-max-x-candidates 1000000 \
  --classical-direct-context-cache \
    /opt/oneshotsea/direct-caches/p125-selected20-tail89-97/direct.ctx \
  --classical-direct-context-sha256 \
    a459dc7732e0a8924f3dcce15bd640c5a72f594d403bf117d0fa45c6b3805625 \
  --classical-direct-context-max-file-bytes 1000000000 \
  --classical-direct-cache-resident-bytes 1000000000 \
  --sea-threads 1
```

The pre-smooth threshold promotes the two-level suffix when the cap-16 set is
still multiple; zero retains the conservative post-smooth policy. The 1 GB
logical residency budget keeps this 150,799,468-byte context resident across a
cohort and is a resource setting rather than checkpoint identity.

The launcher and remote worker fail closed unless the cache file and manifest
match the trusted digest, target, ordered schedule, caps, exact preparation
command, file size, deployed commit and binary, and `sea-threads`. Supplying an
expected digest during preparation additionally checks reproducibility; when
it is initially unknown, record the emitted digest through a trusted channel
and provide it to the launcher. The worker copies the direct-cache manifest,
build log, and exact preparation command into its provenance tree; resume
repeats the complete normalized search argv. The 150,799,468-byte cache above
is a measured p125 policy input, not an automatic or asymptotically universal
schedule. Its bounded four-curve no-charge check and deterministic cap-one
replay are retained under
[`artifacts/local/p125-cap-one-direct-tail-20260803`](../artifacts/local/p125-cap-one-direct-tail-20260803).
The threshold-two paired run, exact-trace comparison, and timing scope are
retained under
[`artifacts/local/p125-pre-smooth-direct-tail-20260803`](../artifacts/local/p125-pre-smooth-direct-tail-20260803).

Always fetch artifacts before teardown, then terminate and confirm the final
state:

```sh
scripts/aws/fetch.sh --run-id RUN --execute
scripts/aws/terminate.sh --launch-id LAUNCH --task-tag TASK --fetched-run RUN
scripts/aws/terminate.sh --launch-id LAUNCH --task-tag TASK --fetched-run RUN --execute
```

The fetch record includes instance type, architecture, price, lifetime, and an
estimated upper-bound cost.  AWS Billing/Cost Explorer remains authoritative.
Termination rechecks the `Project`, `LaunchId`, and optional `Task` tags and
verifies the fetched `SHA256SUMS`. Use `--allow-unfetched` only for an explicit
emergency teardown where artifact loss is accepted.
