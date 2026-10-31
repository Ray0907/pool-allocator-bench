#!/bin/sh
# Optional platform leak evidence; an executed check's failure is not hidden.
set -u
mkdir -p results
if [ "$(uname -s)" = Linux ]; then
    status=0
    for binary in test_pool_asan test_mm_asan; do
        ASAN_OPTIONS=allocator_may_return_null=1:detect_leaks=1 ./$binary > "results/leaks-$binary.txt" 2>&1
        code=$?
        printf 'LeakSanitizer %s exit=%s\n' "$binary" "$code" >> "results/leaks-$binary.txt"
        cat "results/leaks-$binary.txt"
        if [ "$code" -ne 0 ]; then status=$code; fi
    done
    exit "$status"
elif command -v leaks >/dev/null 2>&1; then
    ASAN_OPTIONS=allocator_may_return_null=1:detect_leaks=0 leaks --atExit -- ./test_pool_asan > results/leaks-asan.txt 2>&1
    status=$?
    printf 'leaks ASan exit=%s\n' "$status" >> results/leaks-asan.txt
    if [ "$status" -ne 0 ]; then
        leaks --atExit -- ./test_pool > results/leaks-native.txt 2>&1
        printf 'leaks native exit=%s\n' "$?" >> results/leaks-native.txt
    fi
else
    printf '%s\n' 'NOT VERIFIED: leaks is unavailable' > results/leaks-asan.txt
fi
cat results/leaks-asan.txt
if [ -f results/leaks-native.txt ]; then cat results/leaks-native.txt; fi
