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

Query tagged charges for an exclusive date range with:

```sh
scripts/aws/cost.sh --start 2026-08-01 --end 2026-08-02
scripts/aws/cost.sh --start 2026-08-01 --end 2026-08-02 --execute
```

Provisioning requires both a current on-demand price ceiling and a hard
lifetime.  Cloud-init arms a shutdown timer, instance-initiated shutdown
terminates the instance, the root disk is encrypted and delete-on-termination,
and the operator still terminates promptly after fetching artifacts.

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

The full production worker additionally needs the authenticated `p125` smooth
cache under `/opt/oneshotsea/caches`.  `prepare-cache.sh` can reproduce it on
the instance and records a cache manifest; a short thread-scaling trial should
instead use the CAS-free SEA CLI directly so a 30-minute instance is not spent
rebuilding the 5.4 GB cache.

Always fetch artifacts before teardown, then terminate and confirm the final
state:

```sh
scripts/aws/fetch.sh --run-id RUN --execute
scripts/aws/terminate.sh
scripts/aws/terminate.sh --execute
```

The fetch record includes instance type, architecture, price, lifetime, and an
estimated upper-bound cost.  AWS Billing/Cost Explorer remains authoritative.
