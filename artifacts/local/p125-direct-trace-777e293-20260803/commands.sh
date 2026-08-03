#!/usr/bin/env bash
set -euo pipefail

source_commit=777e293786ace30a3b8fec025d90875267f98ea4
project_root=${PROJECT_ROOT:-"$(git rev-parse --show-toplevel)"}
scratch=${SCRATCH:-"$(mktemp -d /private/tmp/oneshotsea-p125-trace-777e293.XXXXXX)"}

git clone --local --no-hardlinks "$project_root" "$scratch/source"
git -C "$scratch/source" checkout --detach "$source_commit"
test -z "$(git -C "$scratch/source" status --porcelain)"

/usr/bin/make -C "$scratch/source" -j4 \
  build/validate_p125_direct_trace \
  >"$scratch/build.stdout" 2>"$scratch/build.stderr"

curl --fail --location --proto '=https' --tlsv1.2 \
  https://ftp.gnu.org/gnu/time/time-1.9.tar.gz \
  --output "$scratch/time-1.9.tar.gz"
mkdir "$scratch/time-src" "$scratch/time-build"
tar -xzf "$scratch/time-1.9.tar.gz" -C "$scratch/time-src" \
  --strip-components=1
(
  cd "$scratch/time-build"
  "$scratch/time-src/configure" --prefix="$scratch/time-install"
  /usr/bin/make -j4
  /usr/bin/make install
)

"$scratch/time-install/bin/time" \
  --output="$scratch/trace.time" \
  --format='command=%C\nexit_status=%x\nelapsed_seconds=%e\nuser_seconds=%U\nsystem_seconds=%S\nmax_rss_kib=%M\naverage_rss_kib=%t\nminor_page_faults=%R\nmajor_page_faults=%F\nvoluntary_context_switches=%w\ninvoluntary_context_switches=%c\nfilesystem_inputs=%I\nfilesystem_outputs=%O' \
  "$scratch/source/build/validate_p125_direct_trace" --threads 4 \
  >"$scratch/trace.ndjson" 2>"$scratch/trace.stderr"

# The retained invocation used scratch directory
# /private/tmp/oneshotsea-p125-trace-777e293.UR71KZ. Copy the two build logs,
# trace.ndjson, trace.stderr, and trace.time into raw/ without transforming
# them, then run audit.py --emit-result, generate SHA256SUMS, and run audit.py.
