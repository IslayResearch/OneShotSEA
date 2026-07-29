# Third-party notices

OneShotSEA's original code is distributed under the root MIT license. The
repository also contains the two pinned, MIT-licensed upstream components
listed below. The root license does not replace their copyright notices.

## Canonical one-shot primality verifier

- Local file: `third_party/oneshot_primality_proofs/voneshot.py`
- Upstream: [AndrewVSutherland/OneShotPrimalityProofs](https://github.com/AndrewVSutherland/OneShotPrimalityProofs)
- Pinned commit: `47d27c2691380c4ecb84f22aaad21f907b84bae4`
- Upstream path: `voneshot.py`
- SHA-256: `e0ba3b8a7ed2ff48bd2fd824642bf67b0954a9f03f57daeb4ac4302691e1b666`
- License: MIT
- Copyright: 2026 Andrew Sutherland

The source and its license are unmodified pinned copies. Exact retrieval and
verification details are recorded in
`third_party/oneshot_primality_proofs/UPSTREAM.md`; the upstream license is
also retained at `third_party/oneshot_primality_proofs/LICENSE`.

```text
MIT License

Copyright (c) 2026 Andrew Sutherland

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## OneShotFastECPP smooth-part engine

- Local files: `third_party/oneshot_fast_ecpp/smooth.c` and
  `third_party/oneshot_fast_ecpp/smooth.h`
- Upstream: [AndrewVSutherland2/OneShotFastECPP](https://github.com/AndrewVSutherland2/OneShotFastECPP)
- Pinned commit: `88da82fbcda4471746b5df34f008dcfa5cc28d2d`
- Upstream paths: `ecpp/smooth.c` and `ecpp/smooth.h`
- SHA-256 (`smooth.c`): `4fe25ccc9dda43a00b042445e9a43080ec282fdfb7e2570b82a2824ef3aa32cb`
- SHA-256 (`smooth.h`): `cf3f9d2c9e2354d4cc8643772cccbf997e34e05c5d5267e0b46ab039ad1cea3c`
- License: MIT
- Copyright: 2026 AndrewVSutherland2

The source files and their license are unmodified pinned copies. Exact
retrieval and verification details are recorded in
`third_party/oneshot_fast_ecpp/UPSTREAM.md`; the upstream license is also
retained at `third_party/oneshot_fast_ecpp/LICENSE`.

```text
MIT License

Copyright (c) 2026 AndrewVSutherland2

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Explicit exclusions

The `classpoly_v1.0.3`, `ff_poly_v2.0.0`, `zp_poly`, and `zn_poly` trees from
OneShotFastECPP carry explicit GPL notices. They are not included in this
repository and are not linked into OneShotSEA by either vendored component.
This notice does not grant permission to copy those excluded trees under the
OneShotSEA MIT license.

GMP, OpenMP runtimes, CUDA, PARI/GP, and Magma are external build, runtime, or
test dependencies rather than vendored project code. Their respective license
terms apply independently.
