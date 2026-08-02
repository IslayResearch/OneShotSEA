# SEA search design for one-shot primality certificates

Status: Section 0 is the authoritative as-built design of the current CPU
production search.  Sections 1--13 retain the original implementation contract
and target architecture for rationale and historical traceability; imperative
or future-tense mechanisms there are not claims about the binary unless
Section 0 or the linked executable-path documents say they are implemented.
In particular, fixed-limb/Newton arithmetic, learned search policies,
level-major cross-curve table reuse, and CUDA SEA kernels are targets, not
current features.

## 0. Authoritative as-built production design

### 0.1 Search identity and curve stream

`oneshotsea search` is input-driven rather than target-hard-coded: it accepts a
probable-prime target greater than seven whose `n^4` bound fits `uint64_t`, plus
a deterministic seed, global half-open index range, and exact worker
partition.  The current X1(11)/X1(27) generators additionally require
`p=1 (mod 4)`; the Weber-f family covers the general supported prime path.  The
production deployment
currently selects `x1-27` with the audited point-order-four filter.  For every
assigned global index the generator deterministically samples the pinned
X1(27) model, validates its exact torsion, full rational `E[2]`, point-four
branch, Weber/Montgomery image, and canonical curve/twist side, and retains the
exact nonexceptional Weber-f source coordinate.  These checks give the selected
side a conservative cyclic divisor 108 and, for `p125`, a proven group-order
divisor 432.  The sign of the resulting exact trace prior is determined only
after the same-j scaling identities identify which side is the canonical SEA
curve.  The program itself is not hard-coded to `p125`: bounds, Hasse interval,
trace residues, and certificate inequalities are derived from the input.

The semantic schedule digest binds curve family/formula and sampling policy,
point-four flag, exact-prior policy, fallback/heuristic policy,
smooth-cache digest, and verifier digest.  The full search/checkpoint identity
separately binds the prime, seed, range partition, authenticated table
manifest, build identity, and schedule digest.  Resource-only choices
such as thread counts may change on resume.  Any semantic identity mismatch
fails before SEA.

### 0.2 Custom SEA and specialized modular-polynomial path

The point counter is project-native C++20 with exact GMP-backed field and
polynomial arithmetic; balanced dense products use an exact limb-aligned
Kronecker convolution above their measured crossover, with Karatsuba and
schoolbook fallbacks. Production does not call Magma, Sage, PARI, FLINT, or
another point counter.  Startup authenticates the checked-in 77-level Weber-f
subset through prime level 401 against a pinned normalized source catalog.
The same catalog supports selectively materialized subsets of all 166
admissible archive levels through 997, with exact filenames, byte counts, and
SHA-256 values.  Each admitted curve begins from its validated retained Weber
source coordinate, directly specializes `Phi_l^f(f,Y) mod p`, finds rational
neighbors, recovers normalized isogenies with the BMSS power-series/Padé path,
and proves Frobenius eigenvalues in the quotient algebra.  Verified 24th-root
source-orbit transport shares one specialization across covariant lifts, and a
validated first stable kernel may derive only its characteristic-polynomial
conjugate.  Both optimizations retain independent reference paths and exact
projection A/B tests.

Levels are processed in increasing prime order.  A measured
information-per-cost schedule remains available only in the low-level tool;
its held-out production A/B was 0.7% slower, so it is not the search default.
Authenticated classical levels 5 and 7 may add certified Atkin factor-degree
constraints.  Exact Elkies residues and the family trace prior form the exact
CRT; Atkin state is separate and can narrow a complete bounded Hasse set but
cannot satisfy the final unique-trace gate.  If the Weber schedule is
insufficient, the explicitly enabled exact Schoof fallback extends the retained
state through a fixed audited prime sequence.  A contradiction or exhausted
implementation limit fails closed.

### 0.3 Sound early abort, exact smoothness, and certificate tail

The first SEA pass proceeds to early screening only when the
exact-plus-certified constraint state represents at most the configured trace
cap (16 in the current `p125` deployment).  It can instead fail closed on a
missing rational lift or exhausted implementation limit.  For screening it
enumerates the complete Hasse-compatible set, forms both orders `p+1-t` and
`p+1+t` for every trace, and extracts their exact
`n^4`-smooth parts against one authenticated full prime-product cache.  If all
parts are at most the verifier lower bound `L`, rejection is mathematically
sound: any admissible exact point order `m>L` would divide one of those exact
smooth parts.  With exact Schoof fallback enabled, a survivor extends the same
retained state toward uniqueness; without fallback, the second trace-cap-one
pass recomputes SEA from the authenticated schedule.  Smoothness never creates
a trace congruence.

The production search has no `--search-policy` option and no learned score or
partial-smoothness rejection.  Its explicit, schedule-bound
`--skip-incomplete-curves 1` may advance past
`implementation_no_lift` or `implementation_level_limit` and is always logged
as a heuristic rejection.  The sound RunPod shard uses fallback on and this
skip off.  Certificate construction also has a finite deterministic
`--assembly-attempts` budget per coefficient: exhaustion is recorded as
`certificate_assembly_failed` and can miss a realizable point even though it
cannot emit a false certificate.  Candidate/node enumeration caps differ:
they stop without advancing, so a resume with larger caps rechecks the curve.

For a unique order, the native tail exhaustively enumerates bounded admissible
smooth divisors, tries curve and twist Montgomery classes, constructs primary
components, and proves exact point order with `[m]P=O` and `[m/q]P!=O` for each
prime divisor.  It checks `L<m<L*r`, recomputes exactly the required sorted
large-prime list, validates the Montgomery coordinate, and atomically publishes
only after the unmodified pinned `voneshot.py` accepts the one-line certificate.

### 0.4 Concurrency, memory, and durable evidence

Rolling curve concurrency shares one immutable smooth cache but commits curve
records, checkpoints, and any winning certificate strictly by increasing
global index.  Each curve currently walks its own increasing SEA level
sequence; level-major cross-curve table reuse is not implemented.  Optional
smooth coordinator cohorts can group requests that arrive during another
cache scan, but remain disabled until a clean bracketed throughput gate passes.
The full product, root auxiliary tables, per-curve polynomial state, candidate
enumeration, and worker fan-out all have explicit caps.  A partial curve is
recomputed after a crash rather than serializing mutable polynomial state.

Machine-readable curve records distinguish sound, heuristic, assembly, and
implementation-limit outcomes and include exact/Atkin/fallback counts,
candidate counts, full point counts, major-kernel and optional per-level
timings, memory, and monotone state counters.  Run manifests bind source,
binary, command, table, cache, range, and seed.  Separate provision/fetch
metadata records pod or instance hardware, rate, lifetime/spend estimate,
checksums, and resource logs.  The exact commands and operational boundaries
are in `docs/search_pipeline.md`, `docs/runpod.md`, and `docs/aws.md`.

### 0.5 Asymptotic expectation and measured boundary

Let `n=ceil(log2 p)`, `B=n^4`, and
`L=sqrt(p)+2*p^(1/4)+O(1)=p^(1/2+o(1))`, as follows directly from the
verifier's nested-square-root bound.  The one-shot smooth-divisor heuristic
used by this approach predicts that a curve or its twist supplies the required
`B`-smooth divisor with probability `p^(-1/8+o(1))`.  The expected search is
therefore `p^(1/8+o(1))` curves.  Heuristically, enough small SEA primes have
product above the Hasse width after total log-prime mass `Theta(log p)`; with
directly instantiated modular functions, point counting contributes factors
polynomial in `log p` per curve and is absorbed by the `o(1)` exponent.  This
is the intended asymptotic separation from discriminant enumeration in the CM
route, whose search term is `p^(1/4+o(1))`.

Those exponents are not runtime promises.  At 416 bits the constants are
dominated by quotient-polynomial Frobenius/eigenvalue work and exact scans of a
5.4 GB smooth prime product.  The retained X1(27) cyclic-divisor planning model
uses probability `0.000010446455027346424`, or about 95,726 curves, but it omits
curve/twist dependence, group-exponent restrictions, representability, and
exact-order assembly.  The isolated 30-curve RunPod probe measured 26.7
elapsed seconds per curve including cache startup.  These are an optimistic
capacity model and an observed throughput, respectively, not a measured
certificate rate.

## 1. Decisions

The original target architecture made the following choices.  Where an item
is aspirational, the current implementation documents its exact fallback in
the files cited above.

1. Use C++20 for orchestration and a fixed-modulus, fixed-limb `Fp` backend for
   the hot path.  GMP remains the reference backend and handles integers, CRT,
   smooth parts, and certificate-sized scalar arithmetic.
2. Generate Montgomery curves directly,

   ```text
   E_A: y^2 = x^3 + A*x^2 + x,       A^2 != 4 (mod p),
   ```

   and run SEA on the normalized short Weierstrass model.  This guarantees a
   rational 2-torsion point, makes the Weber-invariant descent cheaper, and
   means that a successful curve already has the model required by
   `voneshot.py`.  The current `deterministic_curve` short-Weierstrass sampler is
   retained only for tests.
3. Count a curve and obtain its quadratic twist for free.  If
   `t = p + 1 - #E_A(F_p)`, the two candidate orders are

   ```text
   N_curve = p + 1 - t
   N_twist = p + 1 + t.
   ```

4. Use precomputed Weber modular polynomials as the production specialized
   modular-polynomial path.  Implement the evaluation, descent, normalized
   codomain recovery, Elkies kernel construction, and Frobenius-eigenvalue
   computation in this project.  Do not call Sage, PARI, Magma, or the existing
   `isogeny_weber` implementation.  Classical `j` modular polynomials are a
   slow reference and the required ablation, not the production path.
5. Initially use exact Elkies trace residues for final reconstruction.  Atkin
   information is recorded and may reduce candidate sets, but final trace
   uniqueness must also follow from the product of exact Elkies moduli.  This
   deliberately gives up a modest constant factor for a much smaller trusted
   core.
6. Apply smoothness screening to an exhaustively enumerated, small set of
   Hasse-compatible trace candidates before finishing the last SEA primes.
   Full `n^4` screening is a sound early abort.  Progressive smoothness and a
   learned score may abort earlier, but those are explicitly heuristic and may
   have false negatives.
7. Treat `S_B(N) > L`, where `S_B` is the `B=n^4`-smooth part of `N`, only as a
   necessary order-level gate.  It does **not** imply that the chosen `m` lies
   in the group exponent.  Exact point-order construction is a separate,
   mandatory step.

For the primary targets the fixed bounds are:

| target | `n` | `n^2` | `B=n^4` |
|---|---:|---:|---:|
| `p125` | 416 | 173056 | 29948379136 |
| `p130` | 432 | 186624 | 34828517376 |

All formulas and bounds must still be computed from the input; these constants
are not compiled into the search.

## 2. End-to-end pipeline

For a target odd probable prime `p`, seed `s`, and global curve index `i`:

1. Derive `A = H(p,s,i,"curve-A") mod p`.  Reject `A^2=4`, `j=0`, and
   `j=1728`; increment `i`.  The last two exclusions remove exceptional
   isogeny formulas and discard a negligible part of the search space.
2. Convert `E_A` to short Weierstrass form by setting `x_M = X-A/3`:

   ```text
   E: Y^2 = X^3 + a*X + b
   a = 1 - A^2/3
   b = 2*A^3/27 - A/3.
   ```

   Its invariant is `j=256*(A^2-3)^3/(A^2-4)`.
3. Process scheduled SEA primes in batches of curves.  Every accepted Elkies
   prime appends one proven congruence `t == t_l (mod l)` and updates the
   single-residue CRT pair `(t0,M)`.
4. When the number of traces `t0+kM` in the Hasse interval is at most
   `K_smooth`, enumerate them.  Screen both `p+1-t` and `p+1+t` for an
   `n^4`-smooth part greater than `L`.  Drop impossible trace/side pairs.
5. If no pair survives an exact full-bound screen, reject the curve soundly.
   Otherwise resume SEA only for survivors.  Once `M > 2H`, where
   `H=floor(sqrt(4p))`, exactly one integer in `[-H,H]` is congruent to `t0`;
   this is the trace.
6. Factor the exact smooth part of each surviving order, choose one or more
   candidate divisors `m`, and construct a point of exact order `m` on the
   curve or twist.
7. Convert the point's `X` coordinate back to the Montgomery Kummer coordinate,
   emit `p A x0 m q1 ... qk`, and run the unmodified canonical verifier in a
   separate process.  Nothing is reported as a certificate before that process
   exits successfully.

The worker identity is a partition of global indices, not a PRNG state:

```text
global_index = shard_id + shard_count * local_counter.
```

Thus CPU and RunPod jobs are disjoint and a checkpoint consists primarily of
the target hash, seed, shard tuple, completed counter interval, schedule hash,
table-manifest hash, and statistics.  A partially processed curve may be
recomputed after a crash; checkpoints need not serialize polynomial objects.

## 3. Arithmetic layers

### 3.1 Reference layer

The existing `Field`, `Poly`, `SparseModularPolynomial`, and
`TraceConstraints` types are retained for small tests and oracle comparison.
They use `mpz_class` per field element and quadratic polynomial algorithms, so
they must never be selected by the 416-bit production command accidentally.
The CLI must print the backend and reject `--production --backend=reference`.

### 3.2 Production prime field

`FpCtx` is immutable and shared by all threads processing one target:

```cpp
struct FpCtx {
    uint32_t limbs;                 // 7 at 416-432 bits
    uint64_t p[MAX_LIMBS];
    uint64_t n0;                    // -p^{-1} mod 2^64
    Fp R2, one_mont;
};
struct Fp { uint64_t v[MAX_LIMBS]; };
```

Values are Montgomery residues.  Addition/subtraction are constant-bound
conditional reductions; multiplication is CIOS Montgomery multiplication with
`__int128` on CPU.  Provide inversion, Legendre symbol, Tonelli-Shanks, and
batch inversion.  Every inversion returns either an inverse or a nontrivial
`gcd` with the modulus.  The latter aborts field processing and reports a
factor; it must not be treated as a field element.  This matters because the
program is used while `p` is not yet proved prime.

The first optimized implementation may use GMP `mpn` multiplication behind
the `Fp` API.  Replace it with fixed seven-limb code only after a benchmark.

### 3.3 Polynomials

```cpp
struct FpPoly {
    const FpCtx* fp;
    std::vector<Fp> c;              // ascending, trimmed
};
```

Required operations are `mul`, `sqr`, `divrem`, `mod`, `gcd`, `xpow_mod`,
`powmod`, `compose_mod`, derivative, square-free test, and extraction of linear
roots.  Start with schoolbook multiplication and Euclid for the reference
cutoff, then add Karatsuba and half-gcd.  Degrees in the first target search are
typically below 600; an FFT is not assumed to win.  Modular composition should
use Brent-Kung baby-step/giant-step after profiling identifies it as a hot path.

Every optimized operation is differentially tested against the current GMP
implementation.  Polynomial objects are always normalized: the zero
polynomial has an empty coefficient vector and every nonzero modulus is monic.

## 4. Modular-polynomial strategy

### 4.1 Production choice: Weber `f`

The Weber path is chosen because its modular polynomials are approximately
1728 times smaller than classical `j` polynomials and, unlike a full direct-CRT
instantiation engine, it is proportionate to the levels needed here.  A table
through prime level 509 should normally provide far more than the roughly
210-218 exact trace bits needed; a manifest may extend through 997 for curves
with an unusually sparse run of Elkies primes.

For each curve, choose a Weber value `f` in a finite extension satisfying

```text
(f^24 - 16)^3 - j*f^24 = 0.
```

The rational 2-torsion forced by Montgomery sampling makes the descent tower
substantially cheaper than for a generic short Weierstrass curve.  The
implementation must nevertheless support the general descent described below;
it may not assume that `f` lies in `Fp`.

For each odd prime `l`, instantiate `W_l(f,Y)` and its two partial derivatives.
Do not move all integer coefficients into per-curve storage.  The table loader
reduces a coefficient modulo `p` once, and the evaluator performs Horner steps
for a batch of `f` values.  The table header contains:

```text
magic, format_version, invariant="weber-f", level,
x_degree, y_degree, normalization_id, coefficient_count,
generator_commit, uncompressed_sha256, payload_sha256.
```

The payload is sparse and symmetry-aware.  The loader verifies both degrees,
the expected leading terms, and the checksum before use.

The table generator is a separate reproducible tool implementing the
isogeny-volcano/CRT construction of Broeker-Lauter-Sutherland for the Weber
function: select a suitable order, enumerate the surface and floor isogeny
cycles modulo suitable CRT primes, interpolate `W_l mod r`, and reconstruct the
integer coefficients past the documented height bound.  A slower q-expansion
generator may be retained as an independent check.  Published tables may
bootstrap development but are not distributable final artifacts until their
license and normalization are recorded; `docs/reuse_audit.md` already flags
this issue.

### 4.2 Descent and classification

Let `w(Y)=W_l(f,Y)`.  Roots are mapped back by

```text
j' = (f'^24 - 16)^3 / f'^24.
```

If `f` is in an extension, descend the roots without factoring a degree
`l+1` polynomial over that extension:

1. Find the least `k` in `{1,2,3,4,6,8,12,24}` for which `f^k` is in the
   base field.  Apply the Adams operator to map the roots `f' -> f'^k`.
2. If `f^24` is not in the base field, map roots directly by
   `z -> (z^24-16)^3/z^24` using a modular resultant/minimal-polynomial
   computation.
3. The resulting polynomial `w0` is in `Fp[Y]`.  Compute
   `Yp=Y^p mod w0` and `g=gcd(w0,Yp-Y)`.  Linear factors of `g` give the
   rational `j'` values; recover the associated `f'` by a gcd in the original
   instantiated polynomial.

For a square-free, nonexceptional modular point, two rational isogeny roots are
Elkies and no rational roots are Atkin.  One root, `l+1` roots, a repeated root,
`j` or `j'` in `{0,1728}`, or a zero modular derivative is sent to the
`exceptional_prime` path.  The first implementation simply skips that `l` for
this curve.  Skipping information cannot corrupt a trace and these events are
rare.  Supporting volcanic primes is a later throughput optimization, not a
correctness prerequisite.

### 4.3 Classical baseline

The current sparse `Phi_2` and `Phi_3` files establish the format only.  Add a
small, independently generated set of classical `Phi_l` tables (at least
`l=5,7,11,13,17,19,23,29,31,37,41,43`) and evaluate them by batched Horner in
the first variable.  The benchmark compares, for the same curves and levels:

- bytes read and resident;
- time to instantiate and obtain `Y^p mod phi`;
- root classification and recovered `j'` sets; and
- end-to-end trace-residue time where both paths support it.

The production search must log `modpoly_path=weber`.  A speedup measured only in
a table-generation microbenchmark does not satisfy the specialized-path
requirement.

Direct instantiated evaluation from Sutherland's Algorithm 1 is not the first
implementation choice.  At levels of a few hundred, the small Weber database
is simpler and likely faster.  Reconsider direct explicit-CRT evaluation only
if profiles show table I/O/memory is dominant or the required levels grow well
beyond the manifest.

## 5. Exact Elkies trace residue

### 5.1 Immediate CPU milestone from the current repository

The next patch after the existing `Phi_2`/`Phi_3` classification scaffold should
produce an **exact trace residue**, not add more classification tables.  Build a
slow Schoof residue oracle first, because it is independent of modular
polynomial normalization and will remain the differential oracle for the
Elkies code:

1. Implement odd division polynomials `psi_l` for `l=3,5,7,...` and verify their
   roots against brute-force torsion on small fields.
2. Represent the generic torsion point in
   `Fp[x,y]/(psi_l(x), y^2-(x^3+a*x+b))`.  Store an x-coordinate as a rational
   function of `x` and a y-coordinate as a rational function times `y`; compare
   by cross multiplication so a nonunit denominator cannot be mistaken for an
   inverse.
3. Compute `pi(P)=(x^p,y^p)` and `pi^2(P)+[p]P`.  Try every
   `tau in {0,...,l-1}` and select the unique value satisfying

   ```text
   pi^2(P) + [p]P = [tau]pi(P).
   ```

   This is Schoof's characteristic equation and returns `t mod l`.  It is slow
   (`deg psi_l` is about `l^2/2`) but entirely adequate through `l=13` as a
   test oracle.
4. Feed the single residue into `ExactCRT`, enumerate the Hasse set, and compare
   it with the brute-force/Magma trace after every level.

The gate for this patch is exact residues for every nonsingular curve over a
large sample of small primes at `l=3,5,7`, including zero residues and ramified
discriminants.  The following production Elkies implementation then replaces
the quadratic-degree Schoof quotient with the degree-`(l-1)/2` eigenkernel, but
must agree with this oracle on every common case.

### 5.2 Production Elkies residue

Suppose descent gives a simple root `f'`, its `j'`, and nonzero partials.  For
`E: y^2=x^3+a*x+b`, recover the **normalized** codomain, not an arbitrary curve
with invariant `j'`.  Let `F(f)=(f^24-16)^3/f^24`, and evaluate the Weber
partials at `(f,f')`.  Sutherland's equation (8) gives

```text
Jdot = 18*b*j/a
Jdot' = -W_X(f,f')*F'(f')*Jdot/(l*W_Y(f,f')*F'(f))
mu = Jdot'/j'
kappa = Jdot'/(1728-j')
a' = l^4*mu*kappa/48
b' = l^6*mu^2*kappa/864.
```

Here `F'(f)=24*(3*(f^24-16)^2-j)/f`; the factor 24 cancels.  The
`W_X/W_Y` orientation is essential and is checked against classical-`j`
modular derivatives and independent Velu codomains in the core tests.

All denominators are checked.  These formulas are used only when
`a*b*j*j'*(j-1728)*(j'-1728)*W_X*W_Y*F'(f)*F'(f')` is nonzero;
otherwise skip the level.

Construct the kernel polynomial `h_l`, of degree `(l-1)/2`, with the BMSS
fast-Elkies power-series algorithm.  In precise terms, compute

```text
C(x) = 1/(1+a*x^4+b*x^6) mod x^(4l)
S'(x)^2 = C(x)*(1+a'*S(x)^4+b'*S(x)^6),
S(x) = x + O(x^2),
```

using Newton doubling from

```text
S = x + (a'-a)/10*x^5 + (b'-b)/14*x^7.
```

At each doubling, solve the linear differential correction

```text
2*S1'*S2' - (dG/dS)(x,S1)*S2 = G(x,S1)-S1'^2.
```

From `S=x*T(x^2)`, form `U=1/T^2 mod x^(2l)`, rationally reconstruct its
degree-`l` numerator/denominator, and recover the reversed square root of the
denominator as `h_l`.  BMSS's published precision claim has known edge cases;
use precision `4l+4`, not `2l`, and validate the output instead of trusting
successful reconstruction.

The mandatory kernel checks are:

```text
h_l is monic and square-free
degree(h_l) = (l-1)/2
psi_l mod h_l = 0
the normalized codomain computed from h_l has (a',b').
```

For the Frobenius eigenvalue, work in `R=Fp[x]/h_l` with the generic kernel
point `P=(x,y)`, `y^2=x^3+a*x+b`.  Compute

```text
u = (x^3+a*x+b)^((p-1)/2) mod h_l
pi(P) = (x^p, u*y).
```

Find `lambda in F_l*` with `pi(P)=[lambda]P`.  Use the
Gaudry-Morain meet-in-the-middle method: for
`1 <= r <= ceil(sqrt(l))`, cache x-coordinates of `[r]P` and compare them with
those of `[s]pi(P)`.  A collision gives
`lambda = +/- r/s mod l`; compare the corresponding y multipliers to select
the sign.  Batch-clear denominators when that beats individual inversions.
Finally,

```text
t_l = lambda + (p mod l)/lambda mod l.
```

Before committing the residue, independently check in the quotient algebra
that `pi(P)=[lambda]P` in both coordinates and that
`pi^2(P)-[t_l]pi(P)+[p]P=O`.  Any failure quarantines the curve/level and is a
test failure in debug builds.

## 6. Trace state and Atkin information

The current `TraceConstraints::refine` materializes the Cartesian product of
all allowed residues.  That is acceptable for toy tests but is an exponential
memory hazard.  Replace its production use with:

```cpp
struct ExactCRT { BigInt t0; BigInt M; };       // one Elkies residue per l
struct AtkinConstraint {
    uint16_t l;
    SmallVector<uint16_t> residues;
    uint16_t projective_order;                  // 0 if not computed
};
struct TraceState {
    ExactCRT elkies;
    SmallVector<AtkinConstraint> atkin;
};
```

`ExactCRT` is updated with a balanced representative in `[0,M)`.  Atkin sets
remain separate until candidate enumeration.  For an Atkin level, the exact
projective Frobenius order `r | l+1` can be obtained from the order of the map
`Y -> Y^p mod w0` under modular composition.  Candidate residues may then be
generated without delicate closed-form code: enumerate every `tau in F_l`,
form the companion matrix of `X^2-tau*X+p`, compute its order in `PGL(2,l)`,
and retain exactly those of order `r`.  This costs little for `l<1000` and is
easy to oracle-test.

The table-independent PGL arithmetic and exact residue-set generator are now
implemented and exhaustively tested through level 43, including differential
checks against factor degrees of the classical level-5 and level-7 modular
polynomials.  This is the arithmetic half of the Atkin path, not evidence that
an empty rational Weber-root list is Atkin.  Production integration must first
descend the specialized Weber relation through
`j(z)=(z^24-16)^3/z^24`, certify the descended factor degree/projective order,
and fail closed on repeated roots, collisions, or exceptional descent.

As a production-safe first slice, the independently generated and checksummed
classical `Phi_5` and `Phi_7` tables are used directly.  A specialization
supplies Atkin evidence only when it is monic and square-free of degree
`l+1`, has no linear factor, and every irreducible factor has one common degree
`r | l+1`.  Repeated roots, mixed degrees, rational roots, exceptional source
invariants, absent tables, and untrusted levels supply no Atkin constraint; a
present level-5 or level-7 table with the wrong pinned digest is a hard error.
Thus an empty Weber result is never the premise of the classification.

The resulting constraint is held separately from exact Elkies CRT state.  It
may reduce a complete bounded trace set for sound smoothness rejection, but a
trace-cap-one pass ignores Atkin uniqueness and continues until the exact
Elkies CRT alone identifies the trace.  Full Weber-to-`j` descent remains the
route to extending this optimization beyond the two authenticated classical
levels.

Atkin constraints are ranked by

```text
information = log(l / number_of_residues)
score = measured_information_probability / measured_cost.
```

A meet-in-the-middle CRT enumerator combines only the selected constraints and
intersects the Hasse interval.  Final trace certification does not depend on
them: require `M > 2*floor(sqrt(4p))` from exact Elkies residues.  This policy
can later be relaxed only after a separate audit.

## 7. Early abort tailored to the certificate

### 7.1 Candidate set

For exact CRT state `(t0,M)`, define

```text
T = { t0+kM : -H <= t0+kM <= H }, H=floor(sqrt(4p)).
```

This set is enumerated only if `|T| <= K_smooth`; default `K_smooth=64`, with
`16,32,64,128` benchmarked.  Atkin constraints may produce a smaller heuristic
set, but the sound mode uses the Elkies-only `T`.

For each `t in T`, create two records:

```cpp
struct OrderCandidate {
    BigInt trace;
    enum Side { Curve, Twist } side;
    BigInt N;                    // p+1-trace or p+1+trace
    BigInt smooth_part;          // product of completed rungs
    uint64_t smooth_bound;
};
```

### 7.2 Exact smooth part

Let `P_(u,v]` be the product of primes in `(u,v]`.  Use disjoint rungs starting
at the next power of two above `n^2` and doubling to `B=n^4`; the initial rung
also contains all primes at most that bound.  For a batch of candidate orders,
the Bernstein remainder-tree operation computes the exact smooth part for a
rung.  If `P` is its square-free prime product and `N` an order, then

```text
S_rung = gcd(N, (P mod N)^(2^s) mod N),
2^s >= bitlength(N).
```

The exponent is obtained by repeated squaring.  Products from disjoint rungs
are coprime, so multiplying all `S_rung` values through `B` gives exactly
`S_B(N)`, including prime powers.

Prime-product files are immutable, checksummed, endian-neutral, and identified
by `(lo,hi,generator_version)`.  The native-limb cache format from the CM code
must be versioned before it is shared between ARM and RunPod x86 hosts.

### 7.3 Abort rules

Rules marked **sound** cannot discard a possible certificate:

- **Sound success filter:** at any rung, `smooth_part > L` is sufficient to
  retain the order candidate; later rungs are unnecessary for that candidate.
- **Sound rejection:** after processing every prime through `B`, discard an
  order candidate when `S_B(N) <= L`.
- **Sound curve rejection:** after exact full-bound processing, reject the
  curve only when every curve/twist candidate for every `t in T` was discarded.
  The true trace is in `T` by the CRT invariant.
- **Sound trace pruning:** if some but not all candidates survive, discard the
  failing `(t,side)` pairs and finish SEA.  Do not alter the CRT state based on
  smoothness alone.

The proof of sound rejection is short and should appear in the code comment:
the actual trace `t*` is in `T` by the exact-CRT invariant.  If a certificate
exists on either side, its exact point order `m` divides the corresponding
`N*=p+1-/+t*`; every prime of `m` is at most `B`, so `m` divides `S_B(N*)`.
Since the verifier requires `m>L`, necessarily `S_B(N*)>L`.  Thus finding
`S_B<=L` for both sides of every `t in T` excludes a certificate for this curve.

The last distinction is important: smoothness is a search predicate, not a
proof of a trace congruence.

The full prime product is several gigabytes at these bounds, so exact screening
is performed on batches, not one curve at a time.  The scheduler pauses a batch
when its curves reach the trace-candidate cap, sends all candidate orders to
the smooth engine, and resumes surviving curves.  Profile the saved SEA work
against the bytes read and big reductions; disable the early checkpoint if it
loses.  Even when disabled, the same engine runs after exact point counting.

The following were proposed **heuristic false-negative filters** in the target
architecture, but they are not implemented and there is no
`--search-policy` CLI option:

- stop the smooth ladder early when an empirically calibrated model says the
  chance that primes in `(y,B]` lift any candidate above `L` is below a
  configured threshold;
- drop a curve after cheap SEA levels if its known small-prime divisibility and
  partial-smoothness score fall below the retained batch quantile; and
- limit the number of `(trace,side)` candidates sent to exact screening.

A future model would have to retain completed, exactly labelled batches for the
same bit range plus its features, coefficients, training-log checksum,
threshold, observed false-negative rate, and rejection count.  The only
implemented implementation-limit skip switch is `--skip-incomplete-curves 1`;
it handles missing lifts or exhausted levels, not a smoothness score, and it is
off in the sound production shard.  The separately finite assembly-attempt
budget is documented in Section 0.3.  A heuristic rule may reject work but may
never create a candidate, trace residue, or certificate.

### 7.4 Why classification alone is not a sound abort

Knowing `N mod l` for processed SEA primes only reveals divisibility by those
small primes.  It gives no useful upper bound on the contribution of unprocessed
primes below `n^4`.  Therefore a low partial smoothness score is not a theorem.
Sound rejection begins only after the trace candidates are explicit and their
full `n^4`-smooth parts have been computed.  The implementation and metrics must
not label a score-based rejection as exact.

## 8. From a smooth order to an exact-order point

Factor `S_B(N)` completely.  Every factor is below `B < 2^36`, so deterministic
64-bit primality testing plus trial division/Pollard rho is sufficient.  Verify
that the prime powers multiply back to `S_B`.

To construct a verifier-sized `m`, sort prime factors with multiplicity from
largest to smallest and multiply until the product first exceeds `L`.  If the
last factor is the least prime `r` of the product, the previous product was at
most `L`, giving `m <= L*r`; explicitly reject equality.  Try an odd-only
variant first because the rational 2-torsion often prevents the full 2-part of
the group order from lying in its exponent.  List exactly the distinct prime
divisors of `m` in `(n^2,n^4)` as `q_i`; the verifier requires strict upper
bounds.

`m | N` does not imply that a point of order `m` exists.  Do not paper over this
with retries of `[N/m]P`.  Use primary-component assembly:

1. For each `q^e || m`, let `v=v_q(N)` and `c=N/q^v`.
2. Sample a deterministic random point `P` on the selected short curve and set
   `R=[c]P`, which lies in the q-primary subgroup.
3. Repeatedly multiply `R` by `q` to determine its exact order `q^a`.  If
   `a>=e`, set `Q_q=[q^(a-e)]R`; otherwise retry.  Failure after the configured
   attempts rejects this `m` candidate, never accepts it.
4. Add the `Q_q`.  Their orders are coprime, so the sum has exact order `m`.
5. Independently verify `[m]Q=O` and `[m/q]Q!=O` for every distinct `q|m`.

If a prime exponent cannot be realized, lower or remove it and rerun `build_m`;
this adapts to the actual group exponent without computing the full group
structure.

For the curve side, the Montgomery output coordinate is

```text
x0 = X - A/3.
```

For the twist choose one deterministic nonsquare `d` and use
`E^d: Y^2=X^3+d^2*a*X+d^3*b`.  The corresponding Kummer coordinate on the
twist of `E_A` is

```text
x0 = X/d - A/3.
```

Finally run the same x-only Montgomery ladder formulas used by the canonical
verifier.  Require projective `(X:0)` with unit `X` for `[m]Q`, and a unit
nonzero `Z` for every `[m/q]Q`.  This catches coordinate-map and group-law bugs
before invoking `voneshot.py`.

## 9. Scheduling, batching, and resource split

The implemented production order is increasing `l`.  A profiled alternative
is available in the low-level tool and records median and tail time for table
instantiation, descent/Frobenius, classification, kernel construction, and
eigenvalue.  Its experimental score is

```text
score(l) = observed_Elkies_probability * log(l) /
           observed_total_cost(l).
```

The held-out A/B found that score order 0.7% slower, so production retained
increasing order.  Level-major processing across a curve batch and shared
reduced table/extension setup remain unimplemented targets.  The current
rolling window instead lets each curve walk its own level sequence, shares the
immutable smooth cache, and durably retires results in global-index order.

CPU ownership:

- irregular polynomial gcd, root recovery, BMSS reconstruction, CRT, smooth
  product trees, factorization, and certificate assembly;
- thread-local arenas for polynomial temporaries; and
- one I/O thread for table and prime-product prefetch.

Potential GPU ownership, only after profiling:

- seven-limb field multiply/square in large homogeneous batches;
- batched sparse/Horner Weber evaluation for one level and many curves; and
- batched polynomial powering when degrees and iteration counts match.

Do not begin with one CUDA thread per curve performing an entire SEA state
machine.  Divergence and per-curve polynomial allocation make that a poor
mapping.  A GPU kernel is accepted only if an end-to-end batch benchmark,
including transfers and compaction, beats the CPU and produces byte-identical
residue records.  CPU-only remains a complete production configuration.

## 10. Correctness invariants

These are assertions in debug builds and properties in tests.

1. **Curve identity:** the stored `(A,a,b,j)` satisfies both conversion formulas
   and is nonsingular; the twist uses the same `j`.
2. **Table identity:** every table matches its manifest and normalization; test
   evaluations agree with classical `Phi_l(j,j')=0` on sampled isogeny pairs.
3. **Descent completeness:** the multiset of base-field `j'` values obtained by
   descent equals the base-field roots from a classical polynomial on oracle
   sizes.
4. **Kernel identity:** `h_l` is square-free of degree `(l-1)/2`, divides
   `psi_l`, and yields the normalized codomain.
5. **Residue identity:** every committed `t_l` equals the oracle trace modulo
   `l`; quotient-algebra Frobenius checks pass before CRT mutation.
6. **CRT identity:** the true oracle trace remains in the represented Hasse set
   after every update.  When `M>2H`, enumeration contains exactly one value.
7. **Twist identity:** the two exact orders sum to `2p+2` and use traces `t` and
   `-t`.
8. **Early-abort identity:** a soundly rejected curve has an exhaustive
   Elkies-compatible trace list and every associated order has exact
   `S_B<=L`.  Store an audit record containing these counts and hashes.
9. **Smooth identity:** `S_B|N`; its factorization multiplies back to `S_B`;
   every prime factor is at most `B`; removing `S_B` leaves no factor at most
   `B` in exhaustive test cases.
10. **Point identity:** the assembled point has exact order `m`, `L<m<L*r`, and
    every emitted `q_i` is exactly a distinct prime divisor in the required
    interval.
11. **Acceptance identity:** the unmodified, commit-pinned `voneshot.py` accepts
    the exact one-line artifact.  A local reimplementation is never the final
    oracle.

If an invariant fails in production, quarantine the curve and write a compact
reproducer.  Do not continue with a weaker interpretation.

## 11. Tests and independent oracles

### Unit and property tests

- Field operations versus GMP for random 1- through 8-limb primes, including
  aliasing and values around `0,p,2p`.
- Polynomial multiplication, division, gcd, powering, composition, roots, and
  Adams operators versus the GMP reference backend.
- Table parser corruption, wrong level/normalization, missing leading terms,
  and checksum failures.
- Montgomery-to-short conversion and the inverse x-coordinate map for base and
  twist points.
- `TraceState` CRT and capped enumeration versus brute-force traces for small
  fields.  Include products just below, equal to, and above `2H`.
- Smooth-part extraction versus direct factorization for thousands of random
  64- to 256-bit integers and adversarial prime powers at rung boundaries.
- `build_m` boundary cases `m=L`, `m=L*r`, repeated least primes, and factors
  equal to `n^2` or near `n^4`.
- Exact component point assembly on cyclic and deliberately noncyclic groups.

### SEA differential matrix

For many random primes and curves at 16, 32, 64, 128, 256, and 416 bits:

1. Obtain the exact order and trace from local Magma.  PARI
   `ellcard(ellinit(...))` is a second oracle where practical.
2. For every processed `l`, compare Elkies/Atkin/exceptional classification
   with the Legendre symbol of `t^2-4p mod l` and compare every exact residue
   with `t mod l`.
3. Compare Weber descent root sets to classical modular-polynomial root sets at
   every classical baseline level.
4. Ask Magma to validate the kernel/codomain isogeny on sampled Elkies levels.
5. Complete the custom point count and require `p+1-t` to equal both oracles.

Coverage must explicitly include singular inputs, `j=0`, `j=1728`,
supersingular curves, repeated modular roots, volcanic primes, no-root Atkin
levels, one/two/all-root cases, `lambda=+/-1`, both twist signs, and residue
products at the Hasse threshold.  Exceptional cases may be skipped by the
production algorithm, but tests must prove that they are detected rather than
misclassified.

Magma/PARI scripts are test-only subprocesses.  The production binary is run
in CI with their paths removed to prove it has no hidden dependency.

### Early-abort audit

On at least 10,000 completed small/medium curves, save every intermediate trace
state and run the sound abort offline.  A sound rejection is valid only if the
oracle trace/side is present in the enumerated input and its exact smooth part
is at most `L`.  Run the heuristic policy over the same corpus and report its
false-negative rate separately.  Tests should inject the true trace into the
least favorable boundary position and corrupt one residue to ensure the audit
detects an incomplete candidate set.

### Certificate tail

For every end-to-end candidate:

- independently factor `m` in PARI or Magma;
- verify the point order on the selected curve/twist in Magma;
- run the native x-only checks;
- run the exact upstream `voneshot.py`; and
- save stdout, stderr, command line, git commit, table/checkpoint hashes, and
  hardware record.

## 12. Staged milestones and gates

### M0 - pin acceptance and data provenance

Pin the canonical verifier commit, add its unmodified copy and license, choose
the repository license, and define versioned manifests.  Gate: upstream
verifier self-test and known certificates pass.

### M1 - complete reference arithmetic

Finish the current GMP polynomial API, deterministic Montgomery sampler,
curve/twist conversion, CRT enumeration, and brute-force small point counts.
Gate: property tests and Magma/PARI random curves through 32 bits.

### M2 - custom classical SEA reference

Implement classical modular evaluation, classification, normalized kernel,
Frobenius eigenvalue, and exact Elkies CRT for small levels.  It is acceptable
for this path to be slow.  Gate: hundreds of complete 32- to 128-bit point
counts and every intermediate residue match Magma.

### M3 - Weber specialized path

Implement the table format, invariant lift/descent, partial derivatives,
normalized codomain, and reuse the independently tested kernel/eigenvalue
tail.  Add reproducible table generation/checksums.  Gate: Weber and classical
root sets/residues agree on their common levels; a search run actually logs the
Weber path; ablation shows its time and memory effect.

### M4 - production arithmetic and batching

Introduce fixed-limb `Fp`, optimized polynomial kernels, batch scheduling,
frozen prime schedules, bounded arenas, JSONL metrics, and checkpoint/resume.
Gate: bit-for-bit differential results versus the reference backend through
416 bits and deterministic restart reproduces the same curve/residue stream.

### M5 - smoothness and early abort

Integrate the audited smooth-part engine, portable prime-product caches,
candidate enumeration, exact sound abort, and separately gated heuristic
filters.  Gate: the early-abort audit above has zero false negatives in sound
mode; exact-off, exact-on, and heuristic ablations are reproducible.

### M6 - certificate assembly

Implement exact primary-component point construction, Montgomery coordinate
mapping, native exact-order checks, `m/q_i` construction, and verifier
subprocess integration.  Gate: many small/medium certificates, including
noncyclic group cases, pass the unmodified verifier.

### M7 - CPU target profiling, then optional GPU

Profile end-to-end at 256, 384, and 416 bits.  Optimize only measured hot
paths.  Add a CUDA batch kernel if and only if it improves end-to-end curves
per dollar and passes CPU differential tests.  Gate: performance report has
early-abort, batching, twist sharing, schedule, and Weber/classical ablations.

### M8 - `p125`, audit, and `p130`

Run disjoint deterministic local/RunPod shards, retrieve all artifacts, verify
the winning line locally, reproduce from its checkpoint/range, and perform an
adversarial audit.  Only then is `p125` complete.  Continue unchanged on
`p130`.

## 13. Explicit uncertainty ledger

The following items are not assumptions that may silently enter proof logic.

- **Weber descent degeneracies.**  The generic formulas fail at special or
  singular modular points.  Until every case is implemented, detect and skip
  the level.  The trace CRT remains valid because skipped levels add nothing.
- **Fast-Elkies reconstruction precision.**  Published implementations document
  failures at insufficient precision.  Use `4l+4` and enforce the division-
  polynomial and codomain checks.  A failed reconstruction skips the level.
- **Rational 2-torsion and Weber cost.**  It is expected, and supported by prior
  implementations, to reduce the descent overhead; the exact constant must be
  measured.  It is a performance claim, not a correctness premise.
- **Elkies density and maximum level.**  Half-density is heuristic for scheduling.
  The program extends the table manifest/schedule through the pinned
  source-catalog range rather than assuming level 509 always suffices, and must
  use direct generation/evaluation after the finite level-997 boundary.
- **Smooth-score model.**  It is intentionally incomplete and can miss a
  winning curve.  Only its rejection statistics, never its output, enter the
  proof path.
- **Group exponent.**  Smoothness of the order is not smoothness of the
  exponent.  Exact primary-component construction and order tests are the only
  authority.
- **GPU benefit.**  Multi-limb polynomial work may or may not amortize transfers
  at these degrees.  CPU-only is the baseline and remains fully supported.

## References

- R. Broeker, K. Lauter, and A. Sutherland, *Modular polynomials via isogeny
  volcanoes*, arXiv:1001.0402, especially the CRT volcano algorithm and
  alternative modular functions.
- A. Sutherland, *On the evaluation of modular polynomials*, arXiv:1202.3985,
  especially direct instantiation, Weber/gamma2 variants, batching, normalized
  isogenies, and the SEA application.
- A. Bostan, B. Salvy, F. Morain, and E. Schost, *Fast algorithms for computing
  isogenies between elliptic curves*, for the power-series fast-Elkies kernel.
- P. Gaudry and F. Morain, *Fast algorithms for computing the eigenvalue in the
  Schoof-Elkies-Atkin algorithm*, for the meet-in-the-middle eigenvalue step.
- D. Bernstein, *How to find smooth parts of integers*, for the batch
  smooth-part extraction used by the existing CM implementation.
