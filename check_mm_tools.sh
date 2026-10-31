#!/bin/sh
# Optional TSan evidence, only for the safe one-instance-per-thread workload.
set -u
cd "$(dirname "$0")"
mkdir -p results
{
    echo '$ cc -std=c99 -pedantic -Wall -Wextra -Werror -O1 -g -pthread -fsanitize=thread test_mm.c mymalloc.c pool.c -o test_mm_tsan'
    cc -std=c99 -pedantic -Wall -Wextra -Werror -O1 -g -pthread -fsanitize=thread test_mm.c mymalloc.c pool.c -o test_mm_tsan
    status=$?
    echo "compile_exit=$status"
    if [ "$status" -eq 0 ]; then
        echo '$ ./test_mm_tsan'
        ./test_mm_tsan
        status=$?
        echo "run_exit=$status"
        if [ "$status" -eq 0 ]; then
            echo 'TSan: clean for tested one-instance-per-thread workload; shared mm_t not tested'
        else
            echo 'TSan: NOT VERIFIED (runtime/test failed; inspect diagnostics)'
        fi
    else
        echo 'TSan: UNAVAILABLE (compiler/runtime not available)'
    fi
} > results/tsan-mm.txt 2>&1
cat results/tsan-mm.txt
exit "$status"
