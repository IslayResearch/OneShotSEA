# Exact-smooth cross-curve microbatch gate

On 2026-08-02 a no-delay FIFO coordinator was tested as a possible p125
production optimization. The first exact-smooth request began immediately.
Requests that arrived while its primorial scan was active were combined, without
a sleep or fixed collection window, up to the existing 128-order root cap.
Request boundaries, curve/twist ordering, exceptions, cancellation, deterministic
publication, and checkpoint/certificate ownership were preserved.

The frozen candidate survived an independent correctness review before timing.
Focused exact-smooth, search-pipeline, checkpoint, and CLI tests passed, as did
repeated concurrency stress. The new standard-thread paths were clean under
ThreadSanitizer after narrow suppressions for pre-existing Apple
OpenMP/vendor reports. A public-API prime/cache mismatch and two ambiguous
telemetry cases found during review were fixed before the benchmark.

## Same-range result

The controlled order was B1, A1, A2, B2, where A is the frozen production
binary from source commit `43ca2d7` and B is the reviewed coordinator patch.
Every run used p125 indices `[435,445)`, which production had already retired,
with the same deterministic seed and the production X1(27), point-four,
trace-cap-16, ten-curve-thread, one-SEA-thread, exact-cache, and sound-fallback
configuration.

| implementation | run 1 | run 2 | mean |
| --- | ---: | ---: | ---: |
| frozen baseline A | 218.90 s | 218.79 s | 218.845 s |
| microbatch B | 277.08 s | 276.73 s | 276.905 s |

The complete non-timing projection of all ten curve records was identical in
all four runs (SHA-256
`39d84dc1db615ea2c54cd35613e6453ce280ebcfe8749137f124b57c46f03c0a`).
Both B runs submitted 78 curve/twist orders as ten requests and completed them
without error in three successful scan chunks of 12, 16, and 50 orders.

Despite reducing repeated primorial scans, B achieved only
`0.790325202x` baseline throughput: a 26.530193% wall-time regression. The
saved CPU work did not offset the loss of independent scan parallelism.
Therefore the strict `>1.05x` end-to-end promotion gate failed. The
coordinator was removed from the working source and was not used for
production.

The checked-in artifact directory
`artifacts/local/p125-exact-smooth-microbatch-20260802` contains the exact
candidate patch, launcher, four raw stdout logs, four timing logs, four terminal
checkpoints, hashes, telemetry, and the decision record. To reproduce the
negative experiment, apply `candidate.patch`, build `build/oneshotsea`, retain
the frozen baseline binary at `work/oneshotsea-43ca2d7`, and invoke:

```sh
artifacts/local/p125-exact-smooth-microbatch-20260802/run-one.sh \
  LABEL BINARY OUTPUT_DIRECTORY
```

Use fresh output directories and the balanced B1/A1/A2/B2 ordering. The
launcher refuses to overwrite an existing result directory.
