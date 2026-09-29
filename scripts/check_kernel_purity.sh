#!/bin/sh
# Keep the existing Unix entry point; CTest runs the same portable check directly.
set -eu
exec cmake -P "$(dirname -- "$0")/check_kernel_purity.cmake"
