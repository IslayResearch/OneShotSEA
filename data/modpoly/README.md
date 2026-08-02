# Modular-polynomial data

The files under `j/` contain full sparse coefficient lists for the classical
modular polynomials `Phi_2(X,Y)` and `Phi_3(X,Y)`. Each non-comment row is

```text
x_degree y_degree integer_coefficient
```

The coefficients were cross-checked against the symmetric `phi_j_2.txt` and
`phi_j_3.txt` tables distributed with
[OneShotFastECPP](https://github.com/AndrewVSutherland2/OneShotFastECPP).
They are small bootstrap fixtures for the reference evaluator, not the planned
large-level specialized modular-polynomial path.

The `weber_f/` directory is the specialized production table set: 77
admissible prime levels through 401 in full symmetric sparse-row form.  Its
`MANIFEST.json` records the upstream archive and page, normalization, archive
digest, per-file levels, byte counts, and SHA-256 values.  Production search
pins the manifest digest in code and authenticates the complete table set
before processing a curve; missing, extra, or altered `phi_*.txt` files fail
closed.  `tools/generate_weber_modpoly.py` independently cross-checks the
normalization and exact coefficients through level 43.
