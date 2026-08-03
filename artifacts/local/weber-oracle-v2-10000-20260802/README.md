# Retained Weber/Magma oracle corpus

`raw/records.ndjson.gz` is the deterministic gzip (`mtime=0`) encoding of the
complete 10,000-record corpus described by `result.json`.  The adjacent
manifest and early-abort report are the exact original files.  This compact
bundle lets a fresh checkout authenticate and inspect every record without
rerunning the 4,116-second Magma job.

Run:

```sh
python3 artifacts/local/weber-oracle-v2-10000-20260802/audit.py
```

The audit authenticates the compressed bytes, streams and parses every JSON
record, requires contiguous ordinals, recomputes the decompressed byte count
and SHA-256, and checks the manifest, early-abort report, and compact result
bindings.  It does not rerun Magma; the retained manifest records the original
runtime, dependency, source, table, command, and clean-worktree identities.
