# Retained p125 Weber catalog and Magma evidence

This bundle repairs the compact evidence defect in
`p125-weber-catalog-20260802`.  It replays commit `891c9d4` on the exact fixed
X1(27) p125 curve, retains the benchmark binary and build transcript, preserves
the complete native level-409 and level-997 projections, and preserves the
independent Magma input/output and runtime identity.

The corrected Magma order is the 126-digit p125 order in `identity.json`; the
older summary accidentally omitted six zeros.  `audit.py` independently asserts
`order = p + 1 - trace`, the Hasse bound, `trace mod 409 = 19`, the exact prior
`trace mod 432 = 418`, native/Magma curve identity, authentic table manifests,
and checksum coverage of the exact bundle.  Run:

```sh
python3 artifacts/local/p125-weber-catalog-magma-20260802/audit.py
```

`commands.sh` records the complete clean-snapshot reproduction.  The 91 MiB
pinned upstream archive and the 10.8 MiB level-997 table are not duplicated in
Git; their byte sizes and SHA-256 identities are bound by `identity.json`, the
retained source catalog, and the two retained subset manifests.
