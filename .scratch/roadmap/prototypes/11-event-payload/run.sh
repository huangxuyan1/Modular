#!/usr/bin/env bash
# PROTOTYPE — throwaway. One command to run the ticket-11 event-payload probe.
set -uo pipefail
cd "$(dirname "$0")"
ROOT=$(cd ../../../.. && pwd)
OUT=$(mktemp -d)
CXXFLAGS=(-std=c++20 -Wall -Wextra -O2 "-I$ROOT/kernel/include")

echo "--- Part 0: the payload overflow guard is a compile error ---"
if clang++ "${CXXFLAGS[@]}" PROTOTYPE_today_fails.cpp -o "$OUT/fails" 2>"$OUT/err"; then
  echo "UNEXPECTED: it compiled. The payload overflow guard is gone — re-read the probe."
  exit 1
fi
grep -m2 -E "error|static_assert|note: in instantiation" "$OUT/err" | sed 's/^/  /'
echo "  => compile fails, as claimed: the guard is a compile error, not a runtime surprise."

echo
echo "--- Parts 1-4: candidate shapes ---"
clang++ "${CXXFLAGS[@]}" PROTOTYPE_probe.cpp -o "$OUT/probe" || exit 1
"$OUT/probe"
