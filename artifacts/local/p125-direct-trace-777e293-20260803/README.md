# Retained complete p125 direct-trace validation

This bundle retains the complete default
`validate_p125_direct_trace --threads 4` run from an isolated clean clone of
commit `777e293786ace30a3b8fec025d90875267f98ea4`. The validator was built inside
that clone; its source, source tree, executable, compiler, GMP, and GNU time
identities are recorded in `identity.json` and `environment.txt`.

## Result

- All 30 explicitly supplied levels were exact and agreed with the expected
  residue of the separately obtained target trace.
- The exact Hasse-compatible trace count was 226 after `ell=269` and one after
  `ell=271`.
- The unique signed trace was
  `-534284869337319737295513917655253909609824180266230842767530862`.
- The summed preparation time was 1,221.250781 seconds and summed curve
  evaluation time was 60.538477 seconds.
- GNU time measured 1,284.15 seconds elapsed, 4,895.06 user seconds, 5.02
  system seconds, and 889,392 KiB maximum process RSS.
- The largest interpolation-matrix payload was 614,118,960 bytes at
  `ell=271`.

`result.json` is a compact projection recomputed from the raw records. The
unmodified validator stdout is `raw/trace.ndjson`; validator stderr is the
retained zero-byte `raw/trace.stderr`; and `raw/trace.time` is the GNU time 1.9
report. The two clean-clone build streams are also retained without
transformation.

## Reproduction and audit

`commands.sh` records the clean-clone build, temporary GNU time 1.9 build, and
exact retained invocation. The GNU time source archive was downloaded from
`https://ftp.gnu.org/gnu/time/time-1.9.tar.gz` and had SHA-256
`fbacf0c81e62429df3e33bda4cee38756604f18e01d977338e23306a3e3b521e`.

Run the bundle audit from the repository root:

```sh
python3 artifacts/local/p125-direct-trace-777e293-20260803/audit.py
```

The audit verifies complete checksum coverage, source and binary identities,
an empty validator stderr stream, GNU time exit status zero, the exact
30-level schedule, every exact/oracle-accepted residue, progressive exact
moduli, independently recomputed Hasse candidate counts, the 226-to-one final
transition, and equality of `result.json` with the raw projection.

## Independence boundary

The authenticated table-backed path selected the explicit completing schedule;
the schedule is therefore an input to this run. Expected residues and the
final trace are withheld from the direct producer and compared only after each
direct result commits. Both SEA paths share downstream BMSS/Frobenius and
trace-state code. Level-29 Schoof controls elsewhere in the repository cover
two other p125 curves, not this target residue. The separately retained Magma
full point count independently corroborates this exact target curve and final
trace.

This is evidence for a complete point count on one fixed 416-bit curve. It is
not a one-shot primality certificate, a production scheduling result, or an
end-to-end certificate-yield measurement.
