# Deterministic SEA search pipeline

`oneshotsea search` is the production CPU path from a deterministic selected
curve family to a canonical one-shot certificate.  It does not call Magma or
another point-counting oracle.

For each assigned global index it constructs both curve classes for one
Montgomery-compatible Weber-f value, runs the checked-in Weber SEA
implementation and any schedule-bound classical direct tail, extracts exact
curve and twist n^4-smooth parts in one batch, tries both order classes and
both Montgomery sides, retries certificate construction without the 2-primary
part, validates the result natively, and finally invokes the unmodified pinned
`voneshot.py`.  A certificate is reported only after that last process exits
successfully.

The first SEA pass may stop at a bounded, complete set of Hasse-compatible
traces.  This set includes an independently validated exact trace prior when
the selected family supplies one, exact Elkies residues, and optionally
certified factor-degree constraints from the authenticated classical level-5
and level-7 tables. If every curve and twist order in that set has exact smooth
part at or below the certificate lower bound, rejection is sound. Otherwise
the retained state continues with trace cap one; Atkin constraints cannot
satisfy that unique-trace gate, so certificate assembly still requires
uniqueness from the exact prior plus exact Elkies residues. Heuristic rejection
is disabled in this command.

An example local run is:

```sh
./build/oneshotsea search \
  --p 101 --seed 17 \
  --range-start 0 --range-end 100 \
  --worker-id 0 --worker-count 1 \
  --max-level 31 --trace-cap 16 \
  --curve-threads 1 --sea-threads 4 \
  --table-dir data/modpoly/weber_f \
  --smooth-cache runs/p101/smooth.cache \
  --checkpoint runs/p101/worker-0.json \
  --progress runs/p101/worker-0.ndjson \
  --certificate-out runs/p101/certificate.txt
```

The global half-open range is split into deterministic, contiguous, disjoint
worker shards.  A pre-existing checkpoint is resumed automatically and is
accepted only when the prime, seed, worker, shard, schedule, table-content
manifest, cache content, verifier content, and build identity agree.  The
normalized Weber source catalog is pinned by digest, and production startup
requires every record in the selected subset manifest to match that catalog
before authenticating the complete table filename set, byte counts, and
per-file SHA-256 values.  Missing, extra, unknown, forged, or altered tables
fail before a curve is processed.  The
schedule identity explicitly versions the Montgomery-compatibility filter,
the exact trace-prior policy, and the generator-retained Weber source-lift
policy, so checkpoints from an earlier generator, pre-prior schedule, or
pre-source-lift schedule cannot be mistaken for the same deterministic search. The
default build identity hashes the executable.  Every completed curve is
checkpointed by default; partial SEA residues are never trusted after restart.
The Python interpreter is resolved to an absolute executable, content-hashed,
and positively probed as Python 3; verification runs with `-I`.  The production
CLI has no verifier override and accepts only the pinned vendored
`voneshot.py` digest recorded in its upstream manifest.

A smooth cache built by the current process is trusted and immediately bound
into the schedule.  Every use of a pre-existing cache, including a resume,
requires `--smooth-cache-sha256 DIGEST`.  This digest is a trust anchor and must
come from an independently trusted build record; computing it from an unknown
cache merely checks transport integrity and does not establish completeness.
The cache is hashed before and after loading.  Checkpoint, progress, cache,
certificate, and certificate-metadata paths must all be distinct.

On success the canonical certificate line is atomically published before the
cursor advances.  A small sidecar binds its digest and line to the full search
identity and exact global index.  A pre-certificate checkpoint makes recovery
unambiguous even when `--checkpoint-every` is greater than one; recovery
revalidates the sidecar, native certificate conditions, and canonical verifier.

The stdout and progress records distinguish sound smoothness rejection from
`implementation_no_lift` and `implementation_level_limit`.  The latter two are
not mathematical rejections.  Either outcome is reported and checkpointed
without advancing the curve cursor, and the current search chunk stops so the
same index can be retried with an adequate point-count schedule. In
particular, search completeness depends on providing enough valid table,
direct, or exact-Schoof levels to isolate a unique trace for every candidate
that survives early screening. Increasing the range does not remedy an
inadequate point-count schedule.

Long discovery runs may instead opt into
`--skip-incomplete-curves 1`.  This converts only those two incomplete
outcomes into `heuristic_level_limit_skip` or `heuristic_no_lift_skip`, advances
the cursor, and increments only `rejected_heuristic` among the terminal-stage
counters. Orthogonal work milestones such as `candidates_reaching_smoothness`
remain accurate when incompleteness occurs on the second SEA pass. The
progress event sets
`heuristic:true` and `outcome_class:heuristic_rejection`; it is never reported
as a sound rejection or full point count.  The policy is part of the schedule
digest, so its checkpoint cannot be substituted into a sound-only run.  It may
lose a curve that would have yielded a certificate under a larger table set,
but cannot create a false-positive certificate.

The custom evaluator is exposed through an optional, currently local-only
classical direct tail:

```sh
--classical-direct-levels 7,11 \
--classical-direct-max-prime-candidates 1000000 \
--classical-direct-max-x-candidates 1000000
```

Levels must be ordered, distinct primes greater than three. Their order is
semantic and may be chosen from a curve-independent information/cost profile.
After the
authenticated Weber schedule fails to fit the requested trace cap, each level
internally derives its `D=-7*3^(2n)` suitable order and ring class polynomial,
reconstructs `Phi_ell(j,Y)` and its X derivative by witnessed auxiliary-prime
CRT, and consumes the result through BMSS/Frobenius or certified Atkin
factorization. A complete bounded trace set may enter the exact smoothness
screen; a survivor continues the same direct schedule at the exact cap-one
gate. It neither repeats the table pass nor treats Atkin evidence as an exact
trace. The exact/Atkin constraints then flow through the unchanged
smoothness, certificate, and canonical-verifier gates.

The direct policy version, ordered level list, auxiliary-prime candidate cap,
and per-surface x-candidate cap are semantic and included in the schedule
digest. Post-identity mutation therefore fails before curve processing. An
empty list preserves the published default schedule bytes. The two caps bound
failure rather than ordinary resource parallelism: exhausting either is an
implementation limit, never a heuristic or mathematical rejection. Direct
SEA runs before `--schoof-fallback 1`; the fallback sees and checks the same
retained state.

Search order is a separate semantic choice. The default
`--sea-strategy weber-first` retains the published behavior and exact default
schedule bytes: authenticate and consume Weber tables first, then consult the
optional direct tail. `--sea-strategy direct-first` is available only with a
nonempty direct schedule and both authenticated context-cache options. It
starts from the generated curve's exact family trace prior and evaluates the
cached direct levels toward the requested trace cap. This remains a point
count of `pair.curve`; an X1 generator's selected curve/twist side changes the
sign of the validated prior, not which curve is counted.

Only a complete bounded trace set may enter the exact smoothness screen,
whether completed directly or by Weber continuation; the screen still checks
both curve and twist orders. A surviving multi-trace set continues the same
retained state at the exact cap-one gate. If
the direct schedule is inconclusive at either gate, the ordinary Weber pass
continues its retained exact and independently certified Atkin constraints.
It skips every modulus already owned by the direct state and every previously
attempted Weber level. The retained state is bound to the exact normalized
short-Weierstrass coefficients, not merely to the characteristic or
`j`-invariant, so a different curve or same-`j` quadratic twist is rejected
before any constraint or source lift is reused. Direct-cache authentication,
lazy-materialization, or
mathematical exceptions are hard operational failures and never trigger this
continuation. A cap-N enumeration is cleared before cap-one continuation, so
a multi-trace set still cannot leak through the singleton certificate gate.

The nondefault strategy and trusted cache digest are schedule-bound, so a
direct-first checkpoint cannot resume as Weber-first or under another cache.
Direct-first start/curve JSON reports `sea_strategy`, direct-first
attempt/completion/fallback counters, and separate `timings_us.direct_first` and
`timings_us.direct_first_fallback` fields; `timings_us.sea` remains their
inclusive SEA total. Weber-first deliberately omits all of those new keys so
the strict `oneshotsea.search-start.v1` and `oneshotsea.search-curve.v1`
field sets remain byte-compatible with existing production auditors.

The current RunPod and AWS launchers and the strict retained-run auditor do
not admit the direct schedule/cache options or the standalone direct-level schema.
Consequently this tail must remain disabled in remote production runs until
those operational paths receive their own compatibility and adversarial
gates. The default empty direct schedule remains byte-compatible with the
audited cloud schema.

For a local multi-curve invocation, the target characteristic, ordered level
list, and both caps are retained in one run-scoped context. Each level's
curve-independent suitable order, witnessed CRT primes, class polynomials,
and admitted CM surfaces are prepared lazily under a sticky once-only gate,
then shared read-only across curve workers. Independent auxiliary-prime
surfaces are prepared under the same `--sea-threads` resource ceiling used by
the table path; zero selects hardware concurrency and the actual worker count
is capped by the witness count. Indexed result slots and ordered failure replay
preserve deterministic context bytes and deterministic failure selection.
After complete surface admission, each witness is compacted to its Lagrange
and neighbor-coefficient matrices; auxiliary curves, kernels, and isogenies
are not retained. Per-curve target-j matrix evaluation, CRT combination,
BMSS/Frobenius, and Atkin checks remain independent. A later level that no
curve reaches is not prepared and cannot fail the run.

An explicit preparation command can instead materialize the complete direct
schedule into one portable artifact:

```sh
./build/oneshotsea prepare-classical-direct-context \
  --p P --classical-direct-levels 7,11 \
  --sea-threads 4 --output runs/direct-7-11.ctx
```

The command prints the artifact SHA-256. A matching search may supply
`--classical-direct-context-cache PATH` and
`--classical-direct-context-sha256 TRUSTED_DIGEST`. Both options are required
together. The loader authenticates the complete file, re-derives the order,
bound, and deterministic witness stream, validates compact matrix structure,
and structurally indexes the artifact without retaining its matrices. Reached
levels are reauthenticated and materialized lazily. The default releases each
level after its active workers finish; the resource-only
`--classical-direct-cache-resident-bytes N` option retains a bounded LRU of
logical matrix payload across curves. It never silently falls back to
reconstruction when a supplied artifact fails. The digest is included in the
schedule identity. The whole-file admission ceiling defaults to 4 GiB; larger
artifacts require `--classical-direct-context-max-file-bytes N` during both
preparation and search. A nondefault ceiling is identity-bound because it
changes which input files can be accepted. The residency budget is not
identity-bound, so cached and uncached checkpoints cannot be substituted while
LRU resource tuning remains resumable. Full format and trust details are in
[the authenticated direct-context cache note](direct_context_cache.md).

`oneshotsea.search-summary.v1` adds
`classical_direct_preparation.context_count`, `elapsed_us`, `thread_limit`,
`matrix_coefficients`, `matrix_payload_bytes`, `cache_loaded`, and
`cache_load_us`, lazy level-load counters, and retained-context/LRU telemetry
only when the direct schedule is configured. The matrix fields
report the exact compact `uint64_t` payload, excluding witness metadata,
vector headers, and allocator overhead. A cached run reports zero preparation
time and the separately measured authenticated load time.
The elapsed value is the cumulative setup time for contexts actually prepared.
Direct per-level timings exclude setup, while the enclosing curve `sea` wall
time includes any first-use preparation or wait experienced by that worker.
Benchmarks must therefore report the summary and per-curve values together;
reduced warm-curve time alone is not a complete speedup claim. On the checked
416-bit X1(27) fixture, a
reverse-bracketed run
measured 3.03--3.21 s of serial level-7/11 setup and 0.924--0.931 s with four
preparation workers. The main cold evaluation took 0.969 s and the second
curve using the same contexts 0.099 s. This is a bounded local regression, not
a throughput distribution.

The retained representation and isolated level-13/29 RSS bracket are detailed
in [the compact direct-context note](direct_context_compaction.md).

Sound-only runs may instead opt into `--schoof-fallback 1`. After all
authenticated Weber levels are exhausted without fitting the trace cap, this
extends the retained exact/Atkin state with the fixed exact-Schoof sequence
`3,5,7,11,13,17,19,23,29,31,37`, stopping as soon as the complete exact and
effective Hasse sets fit. It skips moduli already exact, verifies an exact upgrade against any
existing Atkin residue set, and rebuilds effective constraints without the
now-redundant Atkin modulus. Contradiction fails closed. A surviving early
screen continues the same state toward uniqueness rather than rerunning the
Weber schedule. The policy is schedule-bound, defaults off, and is independent
of the heuristic incomplete-skip option. Design and target-sized recovery
evidence are in [the Schoof fallback note](schoof_fallback.md).

At run entry the library recomputes the schedule digest from the current
semantic configuration plus the expected smooth-cache and verifier digests
and, when configured, the authenticated direct-context digest.
This rejects post-identity configuration mutation before any curve is
processed; comparing a caller-supplied expected digest to the checkpoint alone
would not establish that binding. Search execution requires authenticated
schedule, cache, table, and verifier identities; empty expected fields are
accepted only while constructing an identity, not while running one.

Every complete curve record retains the exact `trace_prior` modulus and residue
or explicit null.  Every per-level search record retains `trace_residue` for
exact Elkies levels,
the accumulated `exact_modulus`, `exact_trace_candidate_count`, the combined
`constraint_modulus`, effective `trace_candidate_count`, optional
`atkin_projective_order`, `atkin_residue_count`, and the number of compatible
Weber source lifts.  It also logs the actual modular-root worker and orbit
counts, the number of lifts served by exact orbit transport, whether verified
orbit reuse occurred, and the per-stage timings.  Eigen telemetry separately
records total attempts, independent quotient-ring recoveries, and exact
characteristic-polynomial conjugates.  These fields make the production
optimizations auditable without changing residue semantics.

Direct records are kept separately in `classical_direct_levels`. Each retains
its pass and level, exact residue or Atkin order, suitable-order discriminant
and class number, witnessed auxiliary-prime count, Elkies-kernel count,
accumulated exact/effective moduli and candidate counts, and elapsed time. Live
records use `oneshotsea.search-classical-direct-level.v1`. Aggregate direct
pass, level, exact, and Atkin counts remain present even when detailed level
telemetry is disabled.

Long production runs may use `--sea-level-telemetry 0`.  This suppresses the
live per-level records and leaves each curve record's `sea_level_timings` and
`classical_direct_levels` arrays empty, but preserves their aggregate counts,
`final_exact_trace_candidates`, and
`final_trace_candidates` so an incomplete skip remains quantitatively
auditable, along with the per-curve SEA level counts, exact/Atkin counts, trace
prior, status, major-kernel timings, peak RSS, state counters, and checkpoint
behavior. Compact fallback telemetry remains in `schoof_fallback_levels` with
each exact prime, residue, modulus, candidate count, and elapsed time. The
default is verbose and should remain enabled for benchmarks and residue audits.

After obtaining an independent final trace, audit these claims with:

```sh
python3 tools/audit_sea_progress.py \
  --progress runs/p125/worker-0.ndjson \
  --index GLOBAL_INDEX --trace MAGMA_TRACE
```

The auditor independently rebuilds the exact CRT across both table and direct
records, checks every exact residue against the supplied trace, reconstructs
each logged PGL order by repeated matrix multiplication, regenerates its Atkin
residue set, and recomputes both exact and effective Hasse candidate counts
after every level. For direct records it also sanity-checks the signed CM
discriminant, nonzero class number and auxiliary-prime count, and
exact/nonexact kernel-count boundary. It reports the curve/twist orders and
their `2p+2` sum. Retained pre-Atkin v1 records remain auditable with their
original exact-count semantics.

Useful resource caps include `--max-curves`, `--checkpoint-every`,
`--curve-threads`, `--sea-threads`, `--smooth-threads`, `--smooth-max-batch`,
`--smooth-root-auxiliary-bytes`, `--smooth-build-segment-span`,
`--sea-level-telemetry`,
`--classical-direct-max-prime-candidates`,
`--classical-direct-max-x-candidates`,
`--assembly-attempts`,
`--max-certificate-candidates`, and `--max-candidate-search-nodes`.  Reaching
either candidate-enumeration bound aborts before the curve cursor advances, so
the same worker can be resumed with a larger cap without skipping a possible
certificate.  The root-auxiliary cap (128 MiB by default) bounds the two
residue tables used to reduce the full prime product; the immutable product,
order product tree, per-thread GMP scratch, and allocator overhead are
separate.  The segmented-build span bounds the transient sieve/product-tree
memory used while constructing a new full smooth cache; it does not change the
prime product.  A capped run can be resumed with the identical command.
Resource settings that cannot change mathematical output are logged but are
not part of the semantic schedule identity.

The semantic curve family defaults to `--curve-family weber-f`. The opt-in
`--curve-family x1-11` and `--curve-family x1-27` modes deterministically
sample their pinned torsion models until they obtain a rational
Weber/Montgomery image, then pass the resulting curve pair through the same
SEA, exact-smooth, assembly, and canonical-verifier pipeline.
`--x1-require-point4 1` additionally filters for the validated
point-order-four branch.

The exact prior is derived from the curve actually sent to SEA and fails
closed:

- For Weber-f, the short Weierstrass cubic is directly factored first.  Only
  three distinct rational roots establish full rational `E[2]`, hence
  `#E = p+1-t = 0 (mod 4)` and `t = p+1 (mod 4)`.  A curve that does not pass
  this validation receives no mod-4 prior.
- For X1(11), let `D` be the selected-side group-order divisor.  It is 44
  generally, 88 on the validated point-four branch, and 176 when point four
  is present and `p = 5 (mod 8)`.  The last case follows from the retained
  Weber/Montgomery identity: the selected twist class has either full rational
  `E[4]` or a rational order-eight point plus independent `E[2]`.  If the
  selected Tate isomorphism class is
  the canonical curve, SEA starts with `t = p+1 (mod D)`; if it is the
  canonical twist, it starts with `t = -(p+1) (mod D)`.  Since 11 divides
  either `D`, the redundant Weber level `ell=11` is skipped.  The selected-side
  cyclic divisors remain 22 and 44 respectively; the stronger 44/88/176 values
  used by the trace prior are certified group-order divisors, not claims about
  the group exponent or an exact-order-88/176 point.  The conditional proof
  and its counterexample boundary are in
  [the point-four divisor note](x1_11_point4_176.md).
- For X1(27), exact order 27 plus full rational `E[2]` gives group divisor 108;
  point four promotes it to 216, and the retained `p = 5 (mod 8)` Weber branch
  promotes it to 432. The selected curve/twist sign rule is identical, with a
  distinct schedule-bound trace-prior policy. The conservative cyclic divisor
  is 108. Construction and adversarial evidence are in
  [the X1(27) note](x1_27_probe.md).

The prior is inserted into both the exact and effective constraint states, so
it participates in sound bounded screening and the final unique-trace gate.
The family, formula digest, generator version, point-four choice, and trace-
prior policy version are included in the schedule digest; every pre-policy
checkpoint is deliberately incompatible.  Construction and oracle evidence
are in [the X1(11) note](x1_11_probe.md) and
[the X1(27) note](x1_27_probe.md). The retained same-build p125 family
A/B is in [the family comparison](x1_11_family_ab.md): ten observations per
family, zero certificates, and different curve distributions, so it is
throughput evidence rather than an empirical yield estimate.

Both generators also retain the exact nonexceptional Weber-f value from which
the curve pair was constructed.  Production validates that this witness is
canonical, nonzero, unramified, and maps back to the curve's exact j-invariant,
then starts both SEA passes from that singleton source state.  Failure is an
error, never a silent fallback.  Library/CLI callers without a generator
witness retain exhaustive source-lift discovery.  The policy is semantic and
schedule-bound; its correctness audit and paired p125 timing are in
[the known-source note](known_source_lift.md).

`--curve-threads N` evaluates up to `N` consecutive curves concurrently
against one immutable exact-smooth engine and its single authenticated cache.
The default is one.  The coordinator uses a rolling window: it launches a new
index as soon as the lowest pending index has been durably retired, without a
fixed-wave barrier.  Reports, progress records, checkpoints, counters, and a
winning certificate are nevertheless committed strictly in increasing global
index order.  An earlier implementation limit or verified certificate causes
all later speculative reports to be discarded; their futures are joined
before the command exits, so stopping can wait for already-running curve work.
Live per-level JSON can interleave curve indices, but each callback emission is
serialized and partial telemetry never changes durable state.

Curve concurrency is a resource setting rather than a schedule identity: a
checkpoint can safely resume with a different value. `--smooth-coordinators C`
is also resource-only and defaults to zero. Zero retains independent
per-curve exact-smooth calls; a positive value deterministically routes global
index `i` to FIFO cohort `i mod C`, where `C` must not exceed
`curve_threads`. This permits up to `C` grouped cache scans in parallel.

No worker count is silently divided. The operator must budget approximately
`curve_threads * sea_threads` modular-root workers plus, concurrently, either
`curve_threads * smooth_threads` smooth workers when `C=0`, or
`C * smooth_threads` when `C>0`. A zero SEA or smooth thread setting selects
the underlying runtime default and therefore is not a finite one-worker
budget. Coordinator scans overlap remaining SEA work, so setting each factor
at the physical-core count oversubscribes the host. Memory retains one shared
5.4 GB p125 cache, plus concurrent per-curve polynomial state, coordinator
queues, and GMP/OpenMP scratch. The CLI reports the configured coordinator
count and aggregate plus per-cohort scan telemetry; use a measured same-binary
A/B before enabling it on a constrained host.

On the 10-core, 16 GiB Apple M4 p125 host, the retained same-binary scaling
series selected `--curve-threads 10 --sea-threads 1 --smooth-threads 1`:
the fixed warm window reached 27.071 seconds per curve at 6.19 GB reported
peak RSS.  This is a host-specific resource choice, not a universal default.
System-wide swapouts were nonzero during the window, so production operations
retain memory telemetry and do not exceed the physical core count without a
new bounded comparison.

`--sea-threads N` bounds the number of concurrent modular-root jobs within
each Weber SEA level and the number of independent auxiliary-prime jobs while
preparing a classical direct level.  Zero selects the host's reported hardware
concurrency (falling back to one); a positive value is a strict ceiling. The
actual worker count is capped by the number of verified `f^24` source-lift
orbits or direct CRT witnesses, respectively. Long or distributed runs should
set it explicitly so their resource use is reproducible.  The lower-level
`sea-weber-count` command exposes
`--root-orbit-reuse 0|1` and `--conjugate-eigenvalue-reuse 0|1` for controlled
ablations; production search always uses the verified-on, exact-fallback
behavior.  Conjugate reuse derives `p/lambda mod ell` only after complete
rational-isogeny validation and one independent nonzero eigenvalue recovery.
The same lower-level command exposes
`--prime-schedule expected-information-per-cost --level-profile PATH` for a
strict measured scheduling ablation.  The profile must cover every available
level exactly once and every cost must be nonzero.  The held-out `p125` A/B
found this order 0.7% slower than increasing order, so production deliberately
keeps the simpler increasing schedule and does not accept a profile.

The search defaults to `--trace-cap 64` and `--smooth-max-batch 128`: each
complete trace contributes a curve and twist order, so the largest default
early screen is one bounded-smoothness batch. This is a throughput guard, not
a correctness condition; explicit overrides remain available. On the p125
index-0 profile the candidate count jumped from 55,089 at level 281 to 195 at
283 and then directly to one at 307. Screening those 195 traces cost more
than the two remaining SEA levels, so a larger or multi-rung default would
have been slower.

The default remains conservative across curve families, but the p125 X1(11)
point-four production command explicitly uses `--trace-cap 16`.  A same-build
ten-curve ablation measured caps 64, 32, 16, and 1 at 272.19, 271.95, 260.34,
and 260.30 seconds.  Cap 16 reduced complete trace screens from 97 to 24 and
summed concurrent curve work from 1678.006 to 1435.541 seconds.  Cap 1's
0.04-second wall edge is below useful resolution, while its summed work was
1.08% higher than cap 16.  See [the trace-cap note](x1_11_trace_cap.md).
