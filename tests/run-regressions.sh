#!/usr/bin/env bash
# Standalone, local-only regression runs. No install step or external services.
set -euo pipefail
TEST_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODE="${1:-native}"
if (($#)); then shift; fi
TEST_BUILD="${BUILD_DIR:-$TEST_ROOT/build/regressions-$MODE}"
TEST_JOBS="${JOBS:-4}"
CMAKE_ARGS=(-S "$TEST_ROOT" -B "$TEST_BUILD" -DXUTILS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug)
case "$MODE" in
    native|no-ssl|valgrind) CMAKE_ARGS+=(-DCMAKE_C_COMPILER="${CC:-cc}") ;;
    asan|tsan|fuzz) CMAKE_ARGS+=(-DCMAKE_C_COMPILER="${CC:-clang}") ;;
    coverage) CMAKE_ARGS+=(-DCMAKE_C_COMPILER="${CC:-gcc}") ;;
    *) echo "Usage: $0 {native|no-ssl|asan|valgrind|tsan|fuzz|coverage} [ctest options]" >&2; exit 2 ;;
esac
case "$MODE" in
    no-ssl) CMAKE_ARGS+=(-DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=ON) ;;
    asan|fuzz)
        TEST_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
        if [[ "$MODE" == fuzz ]]; then
            TEST_FLAGS='-fsanitize=address,undefined,fuzzer-no-link -fno-omit-frame-pointer'
            CMAKE_ARGS+=(-DXUTILS_BUILD_FUZZERS=ON)
        fi
        CMAKE_ARGS+=("-DCMAKE_C_FLAGS=$TEST_FLAGS" '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined')
        export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1}"
        export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
        ;;
    tsan)
        CMAKE_ARGS+=('-DCMAKE_C_FLAGS=-fsanitize=thread -fno-omit-frame-pointer' '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread')
        export TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=1}"
        ;;
    coverage) CMAKE_ARGS+=('-DCMAKE_C_FLAGS=--coverage' '-DCMAKE_EXE_LINKER_FLAGS=--coverage' -DXUTILS_TEST_COVERAGE=ON) ;;
esac
cmake "${CMAKE_ARGS[@]}"
cmake --build "$TEST_BUILD" -j "$TEST_JOBS"
case "$MODE" in
    valgrind)
        command -v valgrind >/dev/null
        export XUTILS_TEST_TIMEOUT_MS="${XUTILS_TEST_TIMEOUT_MS:-60000}"
        ctest --test-dir "$TEST_BUILD" -T memcheck --output-on-failure \
            --overwrite 'MemoryCheckCommandOptions=--leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect --track-origins=yes --error-exitcode=99' "$@"
        ;;
    tsan) ctest --test-dir "$TEST_BUILD" --output-on-failure -R '(thread|type)_regression' "$@" ;;
    fuzz)
        mkdir -p "$TEST_BUILD/artifacts"
        "$TEST_BUILD/tests/fuzz_corpus" "$TEST_BUILD/corpus"
        "$TEST_BUILD/tests/fuzz_parsers" "$TEST_BUILD/corpus" -max_total_time="${FUZZ_TIME:-60}" \
            -max_len=65536 -timeout=5 -rss_limit_mb=2048 -artifact_prefix="$TEST_BUILD/artifacts/" "$@"
        ;;
    *) ctest --test-dir "$TEST_BUILD" --output-on-failure "$@" ;;
esac
