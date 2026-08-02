#!/usr/bin/env bash
set -euo pipefail

project_root=${PROJECT_ROOT:-"$(git rev-parse --show-toplevel)"}
audit_root=${AUDIT_ROOT:-/private/tmp/oneshotsea-p125-weber-magma-891c9d4-replay}
magma=${MAGMA:-/Users/agent/Documents/Codex/t24-search/private/magma-local/install/magma}
archive=${WEBER_ARCHIVE:-/private/tmp/oneshotsea-phi1.tar.gz}

test ! -e "$audit_root"
mkdir "$audit_root"
git -C "$project_root" archive 891c9d4 | tar -x -C "$audit_root"

python3 "$audit_root/tools/fetch_weber_tables.py" \
    --archive "$archive" \
    --source-catalog "$audit_root/data/modpoly/weber_f/SOURCE_CATALOG.txt" \
    --levels 409 --output "$audit_root/table-409"
python3 "$audit_root/tools/fetch_weber_tables.py" \
    --archive "$archive" \
    --source-catalog "$audit_root/data/modpoly/weber_f/SOURCE_CATALOG.txt" \
    --levels 997 --output "$audit_root/table-997"

/usr/bin/time -l make -C "$audit_root" -B -j10 \
    build/benchmark_p125_poly_trusted \
    >"$audit_root/build.log" 2>"$audit_root/build.stderr"

/usr/bin/time -l "$audit_root/build/benchmark_p125_poly_trusted" \
    sea "$audit_root/table-409" 409 \
    >"$audit_root/native-409.stdout" 2>"$audit_root/native-409.stderr"
/usr/bin/time -l "$audit_root/build/benchmark_p125_poly_trusted" \
    sea "$audit_root/table-997" 997 \
    >"$audit_root/native-997.stdout" 2>"$audit_root/native-997.stderr"

p=100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237
a=24695916746375285203490140593970257817161480924700052157539187724647340231554000845239547321663901475964613358534920636619155
b=16463944497583523468993427062646838544774320616466701438359458483098226821036000563493031547775934317309742239023280424412770
/usr/bin/time -l python3 "$audit_root/oracle/point_count.py" \
    --magma "$magma" "$p" "$a" "$b" \
    >"$audit_root/magma-point-count.json" \
    2>"$audit_root/magma-point-count.stderr"
printf 'quit;\n' | "$magma" \
    >"$audit_root/magma-version.stdout" \
    2>"$audit_root/magma-version.stderr"
