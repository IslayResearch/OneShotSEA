// Independent Magma oracle for a short Weierstrass curve
//
//     E: y^2 = x^3 + a*x + b over GF(p).
//
// The wrapper supplies p, a, and b in private environment variables.  This
// file is intentionally independent of the production implementation.

if not assigned p and GetEnv("ONESHOT_SEA_ORACLE_P") ne "" then
    p := StringToInteger(GetEnv("ONESHOT_SEA_ORACLE_P"));
end if;
if not assigned a and GetEnv("ONESHOT_SEA_ORACLE_A") ne "" then
    a := StringToInteger(GetEnv("ONESHOT_SEA_ORACLE_A"));
end if;
if not assigned b and GetEnv("ONESHOT_SEA_ORACLE_B") ne "" then
    b := StringToInteger(GetEnv("ONESHOT_SEA_ORACLE_B"));
end if;

if not assigned p or not assigned a or not assigned b then
    error "point_count.m requires integer variables p, a, and b";
end if;

if Type(p) ne RngIntElt or Type(a) ne RngIntElt or Type(b) ne RngIntElt then
    error "p, a, and b must be integers";
end if;

if p le 3 or not IsPrime(p) then
    error "p must be a prime greater than 3";
end if;

F := GF(p);
aa := F ! a;
bb := F ! b;
discriminant_factor := 4*aa^3 + 27*bb^2;
if IsZero(discriminant_factor) then
    error "the curve is singular modulo p";
end if;

E := EllipticCurve([ aa, bb ]);
order := #E;
trace := p + 1 - order;

// Decimal JSON integers preserve the exact values for arbitrary-size parsers.
SetAutoColumns(false);
SetColumns(0);
printf "{\"p\":%o,\"a\":%o,\"b\":%o,\"order\":%o,\"trace\":%o}\n",
       p, Integers() ! aa, Integers() ! bb, order, trace;
