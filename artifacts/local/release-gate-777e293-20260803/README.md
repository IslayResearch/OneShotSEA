# Clean-checkout release gate for `777e293`

This artifact records the release gate executed from an isolated detached
clone at commit `777e293786ace30a3b8fec025d90875267f98ea4` (tree
`b964a59a0744236574c00c94088e286be16be3a1`). The clone remained free of
tracked changes and the shared development worktree was not used.

The full `make test-all` gate used the explicit local Magma V2.29-1 runtime.
It exited with status 0 after 171.80 seconds, including all retained artifact
and coverage audits, the canonical verifier and vendor checks, the RunPod/AWS
contracts, and eight live Magma differential tests.

A focused AddressSanitizer/UndefinedBehaviorSanitizer gate also exited with
status 0 after 135.59 seconds and emitted no sanitizer diagnostics. Exact
commands, timings, environment identities, and output hashes are retained in
`result.json`.

This validates the current source tree; it does not change the frozen commit
and binary identity of the active p125 production search.
