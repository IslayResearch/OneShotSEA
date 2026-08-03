# Clean-checkout release gate for `0ff26f6`

This artifact records the release gate executed from an isolated local clone,
not from the shared development worktree. The clone had no tracked changes and
was detached at source commit
`0ff26f68d6766e97fb6faceaa553c5b658ef83fa` (tree
`65a44970cc1769c220695f5267bd3aadc77389ca`).

Exact command:

```sh
MAGMA='/Users/agent/Documents/Codex/t24-search/private/magma-local/install/magma' make test-all
```

The command exited with status 0. It passed the native correctness and search
suites, retained artifact audits, canonical-verifier fixtures, eight Magma
differential tests, and the RunPod and AWS operational contract tests. The
test shell resolved `python3` to the system Python 3.9.6, which also exercises
the repository's older-Python compatibility.

This is a source release gate. It does not change the identity of the active
p125 production search, which remains pinned to deployment commit `550815e`
and its separately retained binary and table hashes.

See `result.json` for exact source, tool, host, and output-binary identities.
