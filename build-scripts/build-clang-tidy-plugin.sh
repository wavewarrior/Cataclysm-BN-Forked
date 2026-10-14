#!/usr/bin/env bash

set -exo pipefail

LLVM_PREFIX=${LLVM_PREFIX:-/usr/lib/llvm-22}
NUM_JOBS=${NUM_JOBS:-$(nproc)}
# Next to the linux-fast build, where `deno task tidy:changed` looks for it.
BUILD_PATH=${BUILD_PATH:-"out/build/linux-fast/tools/clang-tidy-plugin"}

echo "Using bash version $BASH_VERSION with $NUM_JOBS jobs"
echo "Building the clang-tidy plugin at $BUILD_PATH against $LLVM_PREFIX"

# The plugin is its own CMake project: it needs LLVM and Clang development files only, not the
# game's dependencies.
cmake \
  -S tools/clang-tidy-plugin \
  -B "$BUILD_PATH" \
  -G Ninja \
  -DCMAKE_C_COMPILER="$LLVM_PREFIX/bin/clang" \
  -DCMAKE_CXX_COMPILER="$LLVM_PREFIX/bin/clang++" \
  -DCMAKE_PREFIX_PATH="$LLVM_PREFIX" \
  -DCMAKE_BUILD_TYPE=Release

ninja -C "$BUILD_PATH" -j"$NUM_JOBS" CataAnalyzerPlugin
