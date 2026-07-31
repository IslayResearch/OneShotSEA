# Trusted exact-smooth cache identities

`TRUSTED_MANIFEST.json` records independently rechecked identities for full
prime-product caches used by production searches.  The multi-gigabyte cache
files are local/cloud artifacts and are not committed.

The 416-bit cache covers every prime through `416^4 = 29948379136`.  It was
built from a missing target by `ExactSmoothEngine::build`, atomically saved in
the portable `OSSMBASE` format, and then checked independently with macOS
`shasum -a 256`.  Its header records `1,297,866,953` primes and a
5,400,759,974-byte product payload.

Production `oneshotsea search` invocations that reuse this cache must pass the
manifest digest with `--smooth-cache-sha256`.  Do not replace that trust anchor
with a digest calculated from an unknown cache.
