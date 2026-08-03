# Clean-checkout release gate for `a6e0072`

This artifact records the release gate executed from an isolated local clone,
not from the shared development worktree. The clone had no tracked changes and
was detached at source commit
`a6e0072ade348cd0f1a4ab8d9d13ad53e389c402` (tree
`b1850731655afc0cba72bd5b8de18993911aaf22`).

The full documented gate was:

```sh
/usr/bin/time -p env \
  MAGMA='/Users/agent/Documents/Codex/t24-search/private/magma-local/install/magma' \
  /usr/bin/make test-all
```

It exited with status 0 after 205.87 seconds. It passed the native correctness
and search suites, retained artifact audits, canonical-verifier fixtures,
eight live Magma differential tests, and the RunPod and AWS contract tests.

A targeted AddressSanitizer/UndefinedBehaviorSanitizer gate also exited with
status 0 after 162.21 seconds and emitted no sanitizer diagnostics. See
`result.json` for the exact command, source, tool, timing, and output-binary
identities.

This is a source release gate. It does not change the identity of the active
p125 production search, which remains pinned to deployment commit `550815e`
and its separately retained binary, cache, table, and verifier hashes.
