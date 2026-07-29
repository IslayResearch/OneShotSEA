# Current task statement

Build a fast, reproducible search program that uses a custom implementation of the Schoof-Elkies-Atkin (SEA) point-counting method to find a one-shot elliptic-curve primality proof for a prime beyond 400 bits.

The motivation is that the SEA search is heuristically expected to take `p^(1/8+o(1))` time, versus `p^(1/4+o(1))` for the CM approach, with the practical crossover believed to be near 400 bits. The objective is to turn that asymptotic advantage into a working search at 416 bits and beyond.

This is an extension of:

- [AndrewVSutherland/OneShotPrimalityProofs](https://github.com/AndrewVSutherland/OneShotPrimalityProofs), which defines the certificate format and supplies the independent verifier;
- [AndrewVSutherland/DANGER3](https://github.com/AndrewVSutherland/DANGER3), which defines the original Pomerance-triple problem; and
- [AndrewVSutherland2/OneShotFastECPP](https://github.com/AndrewVSutherland2/OneShotFastECPP), the existing CM-based certificate generator and the performance baseline to beat or extend.

The primary target is

```text
p125 = nextprime(10^125)
     = 100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237
```

which has 416 bits. After producing and independently verifying a certificate for `p125`, continue with

```text
p130 = nextprime(10^130)
     = 10000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001113
```

which has 432 bits. The implementation must accept an arbitrary input prime; it must not be hard-coded for these two values.

## The proof object

For this task, a one-shot ECPP certificate is a tuple

```text
(p, A, x0, m, q1, ..., qk)
```

with the meaning and validity conditions used by `OneShotPrimalityProofs/voneshot.py`:

- `p` is the odd integer being proved prime and `n = ceil(log2 p)`;
- `0 <= A,x0 < p` and `A != +/-2 (mod p)`;
- for some `B,y0`, the point `(x0,y0)` has exact order `m` on the Montgomery curve

  ```text
  B y^2 = x^3 + A x^2 + x  (mod p);
  ```

- `m` is `n^4`-smooth;
- with `q = floor(sqrt(p))` and `L = q + 1 + floor(2 sqrt(q))`, one has `L < m < L*r`, where `r` is the least prime divisor of `m`; and
- `q1 < ... < qk` are exactly the prime divisors of `m` in `(n^2,n^4)`.

The required output format is the single line accepted by the canonical verifier:

```text
p A x0 m q1 ... qk
```

The final proof is the certificate, not a transcript of a probable-prime test and not merely the fully factored order of an elliptic curve.

## Required approach

Implement the SEA approach to search elliptic curves over `F_p`, determine enough information about their orders, identify orders with a sufficiently large `n^4`-smooth divisor, and construct a point of suitable exact order on a Montgomery curve or its twist.

The core point-counting implementation must be your own. Do not call or wrap the SEA or elliptic-curve point-counting implementation in PARI/GP, Magma, SageMath, FLINT, or another computer algebra system. Magma is installed locally and should be used as an independent correctness oracle on tractable test cases; PARI/GP or SageMath may also be used for oracle comparisons. General-purpose integer, finite-field, polynomial-arithmetic, threading, FFT, and GPU-compute libraries may be used. Existing permissively licensed low-level code from the linked repositories may also be reused with attribution, especially the smooth-part engine, Montgomery certificate assembly, finite-field arithmetic, and canonical verifier. The substantive SEA logic, trace computation, early-abort logic, and integration into the search must be implemented in this project.

The implementation should exploit the fact that this is a search problem rather than ordinary point counting:

1. Process SEA information incrementally and reject unpromising curves as early as soundly possible. Develop an early-abort criterion tailored to the existence of an `n^4`-smooth divisor `m > L`, rather than automatically completing every point count.
2. Treat a curve and its quadratic twist together when useful, since their orders sum to `2p+2`.
3. Batch work across curves or modular-polynomial evaluations when this amortizes setup costs.
4. Order Elkies/Atkin primes and other tests using measured expected information per unit cost, not simply increasing prime order when profiling supports a better policy.
5. Avoid materializing huge classical bivariate modular polynomials when direct instantiated evaluation or a smaller modular function is faster.
6. Use specialized modular-polynomial techniques in a substantive hot path. In particular, study and implement whichever combination is most effective from:
   - Broeker-Lauter-Sutherland, [Modular polynomials via isogeny volcanoes](https://arxiv.org/abs/1001.0402);
   - Sutherland, [On the evaluation of modular polynomials](https://arxiv.org/abs/1202.3985);
   - direct computation of `Phi_l(j(E),Y) mod p`;
   - explicit-CRT/on-demand evaluation; and
   - alternative modular functions such as `gamma2`, Weber, Atkin, or eta invariants whose modular polynomials are substantially smaller.

Do not merely cite these techniques. At least one specialized or directly instantiated modular-polynomial method must be implemented, exercised by the actual search, and benchmarked against a reasonable classical-`j` baseline or ablation.

The early-abort mechanism may include explicitly labeled heuristic filters that produce false negatives, because the goal is to find a certificate rather than enumerate every valid curve. It must never produce false positives. Separate mathematically sound rejection rules from heuristic search filters, test both, and report the observed rejection rate and cost at each stage.

## Correctness and validation

Build correctness from small cases upward. At minimum:

- unit-test finite-field and polynomial operations, modular-polynomial evaluation, root/factor behavior, Elkies/Atkin classification, trace residues, CRT reconstruction, and curve/twist handling;
- compare complete point counts against at least one independent oracle on many random small and medium prime fields, including edge cases;
- cross-check every claimed trace residue against the final trace on test curves;
- test singular curves, exceptional `j`-invariants, repeated modular-polynomial roots, supersingular curves, Elkies and Atkin primes, sign/twist ambiguity, and residue products close to the Hasse threshold;
- validate smooth-part extraction and the construction of `m` independently;
- verify exact point order, Montgomery representability, and every emitted `q_i`; and
- run the unmodified canonical `voneshot.py` from `OneShotPrimalityProofs` on every final certificate. A locally weakened or rewritten verifier does not count.

Use the locally installed Magma as a primary independent oracle for point counts and intermediate SEA results, supplemented by PARI/GP or SageMath where useful. The production search must still work without any computer algebra system.

## Performance and engineering requirements

This is a non-trivial implementation and optimization task. Produce production-quality native code suitable for long searches on the local multicore machine and on additional RunPod GPU workers. Favor asymptotically sound algorithms, but let profiles decide constant-factor tradeoffs at 400-500 bits and decide which kernels belong on CPUs versus GPUs. A GPU implementation should be used where profiling shows a real throughput benefit; do not force inherently serial or irregular work onto a GPU merely to satisfy this requirement.

The search program must provide:

- a documented build that works from a clean checkout;
- configurable thread count, deterministic seeds, and target prime;
- configurable GPU selection and batch sizing when GPU acceleration is enabled, with a CPU-only fallback;
- the ability to partition deterministic, non-overlapping search ranges across the local machine and multiple launched GPU workers;
- checkpoint/resume support for long searches;
- bounded-memory operation with no unbounded accumulation of per-curve state;
- machine-readable progress statistics, including curves attempted, rejection counts by stage, full point counts completed, candidates reaching smoothness testing, and certificates found;
- timing by major kernel and by SEA prime, plus peak-memory reporting;
- reproducible benchmark commands and logs that record CPU and GPU models, worker count, thread count, seed or search range, wall time, CPU/GPU time when available, and relevant parameters; and
- no dependence on a private service or unrecorded precomputation.

### RunPod cloud operations

You are authorized to launch RunPod GPU pods when needed for development, benchmarking, or production searches, and to stop them when finished. Follow the operational pattern documented under “Cloud Operations” in [gpu-throughput-report_20260620.md](https://github.com/alexamclain/Danger2026DataChallenge/blob/main/research/p26/gpu-throughput-report_20260620.md):

1. Prefer a stateful pod over serverless so the job can be supervised through SSH and `tmux` and can retain ordinary filesystem logs.
2. Select an appropriate available GPU after a short price/performance benchmark; Ada-generation GPUs such as an RTX 4090 or RTX 6000 Ada are known-good operational choices, but do not assume they are optimal for the multi-limb arithmetic required here.
3. Copy the minimal source, build files, tables, and configuration needed for the worker to `/workspace`.
4. Compile natively on the pod, using the installed CUDA toolchain and the correct architecture flag for the selected GPU.
5. Run benchmark and production jobs inside `tmux`, using deterministic, non-overlapping worker IDs, seeds, or start/end ranges.
6. Stream compact per-chunk progress and performance logs. Persist checkpoints often enough that a pod interruption loses only a bounded amount of work.
7. Copy logs, checkpoints, benchmark records, and any candidate certificate back to the local workspace. Independently verify candidate certificates locally before declaring success.
8. Stop the pod immediately when its work completes, when it is no longer cost-effective, or when the account runs out of funds. Exhausted RunPod funds stop cloud work, not local implementation, testing, or analysis.

Use existing RunPod credentials and configuration without printing, copying into the repository, or recording secrets in logs. Record pod GPU type, CUDA/compiler versions, price per hour, pod lifetime, effective compute time, search range, throughput, and estimated cost. Provide scripts or precise commands for provisioning, deployment, monitoring, artifact retrieval, and teardown so another authorized operator can repeat the run. The p26 report is an operational precedent, not an arithmetic implementation template: its specialized 96-bit CUDA backend and `X1(16)` kernel are not suitable substitutes for a correct 416-432-bit SEA implementation.

Precomputed tables are allowed if their provenance, format, generator, checksum, and size are documented and if the repository either includes them or provides a reproducible way to obtain or regenerate them. Do not disguise a precomputed answer for either target as a search result.

Profile repeatedly. Once a bottleneck is established, optimize the measured hot path rather than continuing broad speculative redesign. Preserve a slow, simple reference path wherever practical so optimized kernels can be differentially tested.

## Deliverables and completion criteria

A complete result must include all of the following:

1. Source code for the custom SEA search and certificate construction.
2. Automated correctness tests and independent-oracle comparisons.
3. A technical design document explaining the algorithms actually implemented, the early-abort logic, modular-polynomial strategy, asymptotic expectations, and important engineering tradeoffs.
4. Reproducible build, test, benchmark, and search commands.
5. A performance report with ablations that quantify the benefit of early abort, batching, curve/twist sharing, prime scheduling, and the specialized modular-polynomial path.
6. A newly generated certificate for `p125` that the unmodified canonical verifier accepts.
7. The exact command, seed or assigned search range, checkpoint, logs, local and remote hardware descriptions, wall time, CPU/GPU time when available, and peak host/device memory used to find it.

After items 1-7 are complete, immediately continue the same implementation on `p130` and include its verified certificate and run data if found. A `p130` certificate is a stretch result; it is not a substitute for a reproducible `p125` result.

Partial progress is not completion. In particular, the following are insufficient:

- a design document without a working implementation;
- wrapping an existing SEA implementation;
- point counting without certificate discovery;
- a certificate found by the existing CM generator;
- success only on primes at or below the existing roughly 400-bit frontier;
- an unverified tuple, a probable-prime result, or a certificate that passes only a modified verifier;
- benchmarks on toy inputs with no end-to-end target run; or
- code that cannot reproduce its claimed result from the supplied instructions and artifacts.

## Multi-agent execution

Use multiagent v2 aggressively and dynamically. You have up to 64 concurrent agents available. The root agent owns integration, correctness, profiling, and the live search; do not use a fixed allocation such as “N agents per topic.”

- Begin with independent workstreams for SEA mathematics, modular-polynomial strategies, finite-field/polynomial kernels, CPU/GPU kernel partitioning, early-abort criteria, curve and twist generation, smoothness/certificate assembly, distributed search orchestration, and adversarial testing.
- Maintain an explicit dependency and bottleneck registry. Redirect agents as measurements change; do not let many agents repeatedly produce surveys of the same approach.
- Require concrete patches, executable tests, benchmark data, formulas, or counterexamples. Reject vague reports and unimplemented optimization suggestions.
- Keep at least one independent reference implementation or oracle-driven test path for every delicate optimized kernel.
- Use adversarial agents throughout. They should target incorrect trace reconstruction, insufficient CRT modulus, modular-polynomial normalization errors, exceptional invariants, repeated roots, twist sign mistakes, invalid early rejection, nondeterministic races, checkpoint corruption, exact-order failures, and certificates with malformed large-prime lists.
- Integrate continuously. Do not postpone integration until every component is “finished,” and do not allow parallel branches to drift into mutually incompatible APIs.
- When a route stalls, record the exact measured or mathematical blocker and reassign effort. Reopen it only when there is a new mechanism or evidence.
- Once the implementation is correct and profiling identifies the dominant costs, concentrate agents on those costs and on running the real target search.

Do not return merely because a first implementation is too slow or an initial search finds no certificate. Diagnose the yield and cost model, improve the implementation or search policy, and continue. Spend at least 8 hours on implementation, testing, profiling, and real searches before considering a report of an unresolved blocker.

Return only after the `p125` certificate and all required reproducibility artifacts survive adversarial audit. The final response should lead with the verified certificate, then give the repository layout, exact reproduction commands, measured resource use, and the `p130` status.
