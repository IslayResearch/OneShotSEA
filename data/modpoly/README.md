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
