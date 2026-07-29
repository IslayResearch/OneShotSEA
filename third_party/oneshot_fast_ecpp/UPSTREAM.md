# OneShotFastECPP smooth engine

This directory contains only the audited MIT-licensed smooth-part engine from
OneShotFastECPP. The upstream source files and license are unmodified.

| Field | Value |
|---|---|
| Upstream repository | https://github.com/AndrewVSutherland2/OneShotFastECPP |
| Pinned commit | `88da82fbcda4471746b5df34f008dcfa5cc28d2d` |
| Retrieved | 2026-07-29 |
| License | MIT, copyright 2026 AndrewVSutherland2 |

## Vendored files

| Local file | Pinned upstream URL | SHA-256 |
|---|---|---|
| `smooth.c` | https://raw.githubusercontent.com/AndrewVSutherland2/OneShotFastECPP/88da82fbcda4471746b5df34f008dcfa5cc28d2d/ecpp/smooth.c | `4fe25ccc9dda43a00b042445e9a43080ec282fdfb7e2570b82a2824ef3aa32cb` |
| `smooth.h` | https://raw.githubusercontent.com/AndrewVSutherland2/OneShotFastECPP/88da82fbcda4471746b5df34f008dcfa5cc28d2d/ecpp/smooth.h | `cf3f9d2c9e2354d4cc8643772cccbf997e34e05c5d5267e0b46ab039ad1cea3c` |
| `LICENSE` | https://raw.githubusercontent.com/AndrewVSutherland2/OneShotFastECPP/88da82fbcda4471746b5df34f008dcfa5cc28d2d/LICENSE | `3bdaafd94e539791c916708454fa54183ff6f4956d8a9414053c1f33a065c30f` |

The upstream `classpoly_v1.0.3`, `ff_poly_v2.0.0`, `zp_poly`, and `zn_poly`
components are explicitly outside this vendoring scope and are not included.

Verify the pinned byte content from the repository root:

```sh
python3 third_party/oneshot_fast_ecpp/verify_vendor.py
```
