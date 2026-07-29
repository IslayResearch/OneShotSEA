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
- sparse classical modular-polynomial specialization and root classification;
- deterministic curve generation and curve/twist checks;
- incremental CRT trace constraints for early-abort development; and
- an isolated local Magma point-counting oracle.

Build and run the native tests with:

```sh
make
make test
```

Run the independent oracle tests by setting the local Magma launcher:

```sh
MAGMA=/path/to/magma make test-oracle
```
