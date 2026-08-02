// Independent exact-prime gate for deterministic oracle-corpus inputs.

if not assigned p and GetEnv("ONESHOT_SEA_ORACLE_P") ne "" then
    p := StringToInteger(GetEnv("ONESHOT_SEA_ORACLE_P"));
end if;

if not assigned p or Type(p) ne RngIntElt then
    error "prime_check.m requires an integer p";
end if;

SetAutoColumns(false);
SetColumns(0);
printf "{\"p\":%o,\"is_prime\":%o}\n", p, IsPrime(p) select "true" else "false";
