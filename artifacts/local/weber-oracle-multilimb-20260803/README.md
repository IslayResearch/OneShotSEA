# Retained multi-limb Weber/Magma oracle gate

This compact artifact closes the gap between the 16-32-bit random corpus and
the fixed p125 trace.  It retains one deterministic production-Weber curve at
each of 64, 128, 256, and 416 bits.  For every curve, Magma independently
computed the curve and twist orders, the native path completed the point count,
and the live corpus validator checked every emitted exact Elkies residue,
certified Atkin classification, exact Schoof fallback residue, CRT state, and
final trace against Magma.

The accepted four-curve recapture took 196 seconds on the recorded Apple M4
host.  It was built and run from a clean detached checkout of exact commit
`d226d84f3921497ce3a9c91e85e426071311ec24`; the earlier dirty-binary capture
was discarded.  The
11-KiB deterministic gzip contains all normalized records; `raw/manifest.json`
retains the exact command, Magma runtime and dependency identity, table
inventory, and host description.  `raw/inputs` retains the exact native
executable, corpus driver/bootstrap/common sources, and Magma oracle scripts.
The audit also authenticates every checked-in table file against the capture
manifest, so the accepted executable and all redistributable capture inputs are
locally reviewable rather than inferred from a dirty build hash.

The bounded clean-checkout gate does not invoke Magma:

```sh
python3 oracle/retained_weber_corpus.py \
  artifacts/local/weber-oracle-multilimb-20260803
```

It uses bounded reads throughout, authenticates and streams the complete
retained corpus, and then independently replays the deterministic Weber source,
its map to `j`, the canonical nonsingular curve, least-nonsquare twist, exact
2-torsion prior, every cumulative exact/effective CRT transition and Hasse
candidate count, unique final traces, every exact residue, every Atkin
projective order and complete residue set, and every fallback residue.
`tests/test_weber_corpus_audit.py` runs this gate under the existing
`test-weber-corpus` and `test-all` targets and includes self-rehashed semantic
tamper and decompression-bomb regressions.

To recapture with an authorized local Magma installation:

```sh
git clone /path/to/OneShotSEA /tmp/oneshotsea-multilimb-clean
cd /tmp/oneshotsea-multilimb-clean
git checkout --detach d226d84f3921497ce3a9c91e85e426071311ec24
/usr/bin/make -j4 build/oracle_weber_audit
test -z "$(git status --short)"
MAGMA_ROOT=/path/to/magma-install
python3 oracle/weber_corpus_audit.py \
  --magma-runtime "$MAGMA_ROOT/bin/magma.exe" \
  --magma-root "$MAGMA_ROOT" \
  --native build/oracle_weber_audit \
  --table-dir data/modpoly/weber_f \
  --output-dir work/oracle/weber-multilimb-20260803 \
  --seed 202608030064 \
  --bit-sizes 64,128,256,416 \
  --curves-per-size 1 \
  --max-level 401 \
  --trace-cap 16 \
  --sea-threads 4 \
  --command-timeout-seconds 3600 \
  --max-output-bytes 8388608 \
  --max-prime-attempts 1000000
```

The corpus is deliberately a bounded representative gate, not a statistical
claim about random multi-limb curves.  The separate 10,000-curve artifact
continues to provide breadth through 32 bits.
