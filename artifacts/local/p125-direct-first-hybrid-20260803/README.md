# Fixed-curve p125 direct-first retained-state A/B

This bundle records a one-curve, end-to-end search-pipeline comparison at
`nextprime(10^125)`. Both arms regenerate X1(27) index 2,000,000 from the same
seed, load the same authenticated smooth cache and Weber catalog, and reach the
same sound smoothness rejection.

The candidate arm first consumes 15 authenticated direct classical-`j` levels.
Its four exact and eleven Atkin constraints are retained when Weber evaluation
continues, and Weber skips the overlapping level. The retained effective state
becomes a singleton after 50 Weber levels. The baseline needs 70 Weber levels
and reaches the same rejection with three effective candidates.

## Result

| Metric | Weber first | Direct first + continuation | Ratio |
| --- | ---: | ---: | ---: |
| SEA time | 44.951820 s | 34.659554 s | 1.29695x faster |
| Total curve time | 71.263458 s | 62.308680 s | 1.14372x faster |
| Weber levels | 70 | 50 | 20 fewer |
| Peak RSS | 5,401,657,344 B | 5,401,985,024 B | effectively unchanged |

The direct-first arm spent 1.221256 s in the direct phase and 33.438298 s in
Weber continuation. Its unique trace was

```text
26993842379566050451435984666605440312154846601312600515176078
```

A separate native verifier regenerated the curve and ran the table-backed
Weber path to cap one. It obtained the same trace with one exact and one
effective candidate after 71 levels. This is a separate producer differential,
not an independent mathematical oracle: it shares the downstream SEA code.

An attempted local Magma point count did not start computation because the
local launcher entered an uninterruptible filesystem wait. No Magma agreement
is claimed for this particular curve.

## Provenance and reproduction

- Implementation commit: `8a9a08317eefb5e27246c5decea27adc8e1c2962`
- Implementation tree: `3fa9ca8dee861c561f629dda48f4aafaf750e3c8`
- Search binary SHA-256:
  `aba34a1186b5b09a7649da2ab2f4b5af0e3439f6f19730b059d38efa76d14279`
- Direct-cache SHA-256:
  `b31c858c5398d7b284ad7b003ce4647211de8dec0412421f90ed226f2926ecfd`
- Smooth-cache SHA-256:
  `afe0927dd21aa1555c4b24ecab60636aedf4657c455a4d01ce0e65d863abf551`
- Weber manifest SHA-256:
  `ac1fb3eafd991bccae2fcc05572108f318522b15fd6a3a164b8665c16f2d6bd5`

The raw records preserve the operator-supplied build label
`local:4b646cd-hybrid-ab`; that label is descriptive and is not the source
identity. The commit, tree, and executable digest above are authoritative.

Run `python3 audit.py` to authenticate the retained files and rederive the comparison.
`commands.sh` records the equivalent commands and required external cache
paths. The 5 GiB smooth cache, direct cache, Weber tables, and binaries are not
duplicated in this small bundle.

## Scope

This result validates retained direct-to-Weber state composition on a real
416-bit search curve and measures one concrete latency reduction. It is not a
multi-curve throughput result, a certificate-yield measurement, evidence of a
CM crossover, or proof of the `p^(1/8+o(1))` heuristic.
