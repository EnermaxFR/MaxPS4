#!/usr/bin/env bash
set -euo pipefail

# MaxPS4 Build 51 fast-build helper.
# Keep expensive build outputs in a stable workspace path so actions/cache
# can restore them between GitHub-hosted macOS runners.
CACHE_ROOT="${GITHUB_WORKSPACE}/.maxps4-cache"
mkdir -p "$CACHE_ROOT/ccache" "$CACHE_ROOT/shadps4-build" "$CACHE_ROOT/protobuf-host" "$CACHE_ROOT/downloads"

# Use every CPU made available by the runner instead of a fixed -j2.
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 2)"
echo "MAXPS4_BUILD_JOBS=$JOBS" >> "$GITHUB_ENV"

# ccache is optional: the workflow can install it with Homebrew. Once present,
# CMake/Ninja will transparently reuse unchanged C/C++/ObjC compilation units.
export CCACHE_DIR="$CACHE_ROOT/ccache"
export CCACHE_COMPRESS=true
export CCACHE_MAXSIZE=5G
if command -v ccache >/dev/null 2>&1; then
  ccache --set-config="cache_dir=$CCACHE_DIR"
  ccache --set-config=max_size=5G
  ccache --set-config=compression=true
  ccache -z || true
  echo "CMAKE_C_COMPILER_LAUNCHER=ccache" >> "$GITHUB_ENV"
  echo "CMAKE_CXX_COMPILER_LAUNCHER=ccache" >> "$GITHUB_ENV"
  echo "CMAKE_OBJC_COMPILER_LAUNCHER=ccache" >> "$GITHUB_ENV"
  echo "CMAKE_OBJCXX_COMPILER_LAUNCHER=ccache" >> "$GITHUB_ENV"
fi

echo "MAXPS4_CACHE_ROOT=$CACHE_ROOT" >> "$GITHUB_ENV"
echo "CCACHE_DIR=$CCACHE_DIR" >> "$GITHUB_ENV"
echo "Build 51 fast-build environment ready: jobs=$JOBS cache=$CACHE_ROOT"
