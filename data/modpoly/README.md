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

The `weber_f/` directory contains the checked-in specialized production subset:
77 admissible prime levels through 401 in full symmetric sparse-row form.
`SOURCE_CATALOG.txt` binds the normalized byte count and SHA-256 for all 166
admissible levels through 997 in the pinned upstream archive.  Its own digest
is compiled into the production authenticator.  `MANIFEST.json` selects the
tables present in a particular directory; every selected record must occur in
the pinned catalog, and missing, extra, forged, or altered `phi_*.txt` files
fail closed before a curve is processed.

`tools/fetch_weber_tables.py` verifies the 95,033,052-byte archive digest,
normalizes selected levels, compares them with the source catalog, and emits a
compact manifest.  `tools/generate_weber_modpoly.py` independently
cross-checks the normalization and exact coefficients through level 43.
