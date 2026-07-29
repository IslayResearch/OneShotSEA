# Canonical one-shot primality verifier

This directory vendors the canonical verifier used as the acceptance oracle
for OneShotSEA.

| Field | Value |
|---|---|
| Upstream repository | https://github.com/AndrewVSutherland/OneShotPrimalityProofs |
| Audited commit | `47d27c2691380c4ecb84f22aaad21f907b84bae4` |
| Source URL | https://raw.githubusercontent.com/AndrewVSutherland/OneShotPrimalityProofs/47d27c2691380c4ecb84f22aaad21f907b84bae4/voneshot.py |
| Retrieved | 2026-07-29 |
| `voneshot.py` SHA-256 | `e0ba3b8a7ed2ff48bd2fd824642bf67b0954a9f03f57daeb4ac4302691e1b666` |
| `LICENSE` SHA-256 | `08580c29b868b2aa04f2823732d20656543cc229bef8c9c8aa89e1eec8fdc7e2` |
| License | MIT, copyright 2026 Andrew Sutherland |

`voneshot.py` and `LICENSE` are byte-for-byte copies from the pinned commit.
They must not be modified. Project-specific tests execute the verifier as a
subprocess so no local wrapper can change its acceptance rules.

Run the pin and black-box behavior checks from the repository root:

```sh
python3 third_party/oneshot_primality_proofs/verify_vendor.py
```
