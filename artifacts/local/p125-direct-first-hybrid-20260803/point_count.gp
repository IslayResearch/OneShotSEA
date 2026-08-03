p = 100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237;
a = 71767066679186603923921770935567842539817966722958413189905128110444348984023454005578926582733725886104879149565268081093352;
b = 14511377786124402615947847290378561693211977815305608793270085406962899322682302670385951055155817257403252766376845387395489;

prime_check = isprime(p);
if (!prime_check, error("the supplied modulus is not prime"));
E = ellinit([a, b], p);
gettime();
order = ellcard(E);
elapsed_ms = gettime();
trace_value = p + 1 - order;

print("schema=oneshotsea.pari-point-count.v1");
print("pari_version=", version());
print("prime_check=", prime_check);
print("prime=", p);
print("a=", a);
print("b=", b);
print("order=", order);
print("trace=", trace_value);
print("elapsed_ms=", elapsed_ms);
quit;
