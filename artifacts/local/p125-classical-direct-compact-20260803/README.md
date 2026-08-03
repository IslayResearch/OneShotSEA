# Retained p125 classical direct-context evidence

This bundle retains the raw JSON behind two separate claims made by commit
`7c6a997ddd4482e311b27d6bd3f7e1aa93b909e9`:

1. a same-binary, three-bracket serial/four-worker comparison at levels 7 and
   11; and
2. a same-host compact/pre-compaction bracket at levels 13 and 29, using the
   identical checked benchmark source against each commit's library.

Every timed record is independently validated by Schoof after its measured
interval. The level-7 and level-13 cold records are Atkin cases, so their exact
flags are false while the independently computed Schoof residues are retained
and checked against the admissible residue sets by the harness. The warm
records and all level-11/29 records are exact.

`result.json` states the aggregation rules and the deliberately narrow claim
boundary. In particular, the cross-commit cold timings are not a performance
claim: the long level-29 legs were thermally variable. Peak RSS is
process-wide, while `matrix_payload_bytes` is the exact compact coefficient
payload and excludes container and allocator overhead.

Run the repository auditor after the bundle is tracked:

```sh
/usr/bin/make test-performance-artifacts
```

`commands.sh` records the clean-clone build and bracket structure. It uses
placeholder temporary-clone paths so a replay cannot accidentally depend on
the original ephemeral directories.
