# OneShotSEA

A custom Schoof-Elkies-Atkin search for one-shot elliptic-curve primality proofs beyond 400 bits.

The initial objective is a reproducible one-shot ECPP certificate for `nextprime(10^125)` using an independently implemented SEA point-counting engine, early aborts tailored to certificate discovery, and specialized modular-polynomial techniques. The implementation may use the local multicore machine and deterministic RunPod GPU workers. Magma is available locally as an independent test oracle, but is not part of the production search.

See [revised_prompt.md](revised_prompt.md) for the full task specification, validation requirements, deliverables, and completion criteria.

## Related projects

- [AndrewVSutherland/OneShotPrimalityProofs](https://github.com/AndrewVSutherland/OneShotPrimalityProofs)
- [AndrewVSutherland/DANGER3](https://github.com/AndrewVSutherland/DANGER3)
- [AndrewVSutherland2/OneShotFastECPP](https://github.com/AndrewVSutherland2/OneShotFastECPP)

## Status

Implementation is in progress. The repository currently includes:

- a portable GMP-backed finite-field and polynomial reference layer;
- a native Schoof residue/counting oracle and independent Python oracle;
- level-3 exact Elkies kernel, Vélu codomain, eigenvalue, and trace recovery;
- deterministic curve generation and curve/twist checks;
- exhaustive CRT trace constraints and sound early-abort screening;
- the pinned MIT smooth-part engine with a portable cross-architecture cache;
- a pinned canonical certificate verifier and isolated local Magma oracle; and
- dry-run-safe deterministic RunPod operations.

The native build needs GMP. Smooth-engine tests additionally need OpenMP
(`libomp` with Apple Clang; GCC's OpenMP runtime on Linux). Build and run the
local, CAS-free tests with:

```sh
make
make test test-cli test-reference test-verifier test-vendor \
  test-smooth test-smooth-cache test-runpod
```

Run the full suite, including the independent Magma oracle, by setting its
local launcher:

```sh
MAGMA=/path/to/magma make test-all
```
