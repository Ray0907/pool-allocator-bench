CC = cc
CFLAGS = -std=c99 -pedantic -Wall -Wextra -Werror -O2
SANFLAGS = -g -O1 -fno-omit-frame-pointer
ASAN_OPTIONS = allocator_may_return_null=1:detect_leaks=0
export ASAN_OPTIONS

.PHONY: all test asan ubsan bench util slow mt check-codegen doctor verify mutants clean test-mm test-mm-recent asan-mm ubsan-mm tsan-mm bench-mm memory-mm
all: verify

test_pool: test_pool.c pool.c pool.h pool_util.h
	$(CC) $(CFLAGS) test_pool.c pool.c -o $@

test_pool_fault: test_pool.c test_alloc.c test_alloc.h pool.c pool.h pool_util.h
	$(CC) $(CFLAGS) -include test_alloc.h -Dmalloc=q2_test_malloc -Dfree=q2_test_free -c pool.c -o pool_fault.o
	$(CC) $(CFLAGS) test_pool.c test_alloc.c pool_fault.o -o $@

test_pool_debug: test_pool.c pool.c pool.h pool_util.h
	$(CC) $(CFLAGS) -DPOOL_DEBUG test_pool.c pool.c -o $@

test_pool_asan: test_pool.c pool.c pool.h pool_util.h
	$(CC) $(CFLAGS) $(SANFLAGS) -fsanitize=address test_pool.c pool.c -o $@

test_pool_ubsan: test_pool.c pool.c pool.h pool_util.h
	$(CC) $(CFLAGS) $(SANFLAGS) -fsanitize=undefined -fno-sanitize-recover=all test_pool.c pool.c -o $@

test: test_pool test_pool_fault test_pool_debug
	@mkdir -p results
	@{ ./test_pool && ./test_pool_fault && ./test_pool_debug; } > results/test.txt 2>&1; status=$$?; cat results/test.txt; exit $$status

asan: test_pool_asan
	@mkdir -p results
	@./test_pool_asan > results/asan.txt 2>&1; status=$$?; cat results/asan.txt; exit $$status

ubsan: test_pool_ubsan
	@mkdir -p results
	@./test_pool_ubsan > results/ubsan.txt 2>&1; status=$$?; cat results/ubsan.txt; exit $$status

bench_pool: bench.c pool.c pool.h pool_util.h
	$(CC) $(CFLAGS) bench.c pool.c -o $@

bench: bench_pool
	@mkdir -p results
	@./bench_pool > results/bench.txt; status=$$?; if [ $$status -ne 0 ]; then cat results/bench.txt; exit $$status; fi
	@python3 summarize.py results/bench.txt > results/summary.txt
	@cat results/summary.txt >> results/bench.txt
	@cat results/summary.txt

util_pool.o: pool.c pool.h pool_util.h test_alloc.h
	$(CC) $(CFLAGS) -include test_alloc.h -Dmalloc=q2_test_malloc -Dfree=q2_test_free -c pool.c -o $@

util_pool: util.c util_pool.o pool.h pool_util.h test_alloc.h
	$(CC) $(CFLAGS) util.c util_pool.o -o $@

util: util_pool
	@mkdir -p results
	@./util_pool > results/util.txt; status=$$?; cat results/util.txt; exit $$status

slow_check.o: slow_check.c check_common.h pool.h
	$(CC) $(CFLAGS) -c slow_check.c -o $@

slow_check: slow_check.o pool.c pool.h pool_util.h
	$(CC) $(CFLAGS) slow_check.o pool.c -o $@

mt_check.o: mt_check.c check_common.h pool.h
	$(CC) $(CFLAGS) -pthread -c mt_check.c -o $@

mt_check: mt_check.o pool.c pool.h pool_util.h
	$(CC) $(CFLAGS) -pthread mt_check.o pool.c -o $@

check-codegen: slow_check mt_check check_experiments.py
	mkdir -p codegen
	$(CC) $(CFLAGS) -S slow_check.c -o codegen/slow_check.s
	$(CC) $(CFLAGS) -pthread -S mt_check.c -o codegen/mt_check.s
	python3 check_experiments.py --codegen

slow: slow_check check-codegen
	@mkdir -p results
	@./slow_check > results/slow.txt; status=$$?; cat results/slow.txt; exit $$status

mt: mt_check check-codegen
	@mkdir -p results
	@./mt_check > results/mt.txt; status=$$?; cat results/mt.txt; exit $$status

doctor: test_pool test_mm test_pool_asan test_mm_asan
	@uname -sm
	@$(CC) --version
	@python3 --version
	@command -v make
	@command -v objdump || command -v otool || true
	@command -v valgrind || true
	@command -v leaks || true
	@sh ./leakcheck.sh
	@echo 'TSan: separate optional target make tsan-mm; not a doctor PASS'
	@echo 'Valgrind: not run; Linux uses LeakSanitizer in this target'

mutants:
	@sh ./mutants.sh

verify:
	@$(MAKE) test
	@$(MAKE) test-mm
	@$(MAKE) test-mm-recent
	@$(MAKE) asan
	@$(MAKE) ubsan
	@$(MAKE) asan-mm
	@$(MAKE) ubsan-mm
	@$(MAKE) check-codegen
	python3 check_experiments.py --smoke
	@$(MAKE) bench_pool bench_mm util_pool memory_mm
	./bench_pool --smoke
	./bench_mm --smoke
	./util_pool
	./memory_mm
	$(CC) -std=c99 -pedantic -Wall -Wextra -Werror -fsyntax-only pool.c mymalloc.c

test_mm: test_mm.c mymalloc.c mymalloc.h pool.c pool.h pool_util.h
	$(CC) $(CFLAGS) -pthread test_mm.c mymalloc.c pool.c -o $@

test_mm_fault: test_mm.c mymalloc.c mymalloc.h mm_test_alloc.c mm_test_alloc.h pool.c pool.h
	$(CC) $(CFLAGS) -include mm_test_alloc.h -Dmalloc=mm_test_malloc -Dfree=mm_test_free -c pool.c -o mm_pool_fault.o
	$(CC) $(CFLAGS) -include mm_test_alloc.h -Dmalloc=mm_test_malloc -Dfree=mm_test_free -c mymalloc.c -o mm_fault.o
	$(CC) $(CFLAGS) -pthread -DMM_SHIM test_mm.c mm_test_alloc.c mm_pool_fault.o mm_fault.o -o $@

test_mm_asan: test_mm.c mymalloc.c mymalloc.h pool.c pool.h
	$(CC) $(CFLAGS) $(SANFLAGS) -pthread -fsanitize=address test_mm.c mymalloc.c pool.c -o $@

test_mm_ubsan: test_mm.c mymalloc.c mymalloc.h pool.c pool.h
	$(CC) $(CFLAGS) $(SANFLAGS) -pthread -fsanitize=undefined -fno-sanitize-recover=all test_mm.c mymalloc.c pool.c -o $@

test_mm_recent: test_mm_recent.c mymalloc.c mymalloc.h pool.c pool.h
	$(CC) $(CFLAGS) test_mm_recent.c mymalloc.c pool.c -o $@

test-mm-recent: test_mm_recent
	@./test_mm_recent
	@$(CC) $(CFLAGS) $(SANFLAGS) -fsanitize=address test_mm_recent.c mymalloc.c pool.c -o test_mm_recent_asan
	@./test_mm_recent_asan
	@$(CC) $(CFLAGS) $(SANFLAGS) -fsanitize=undefined -fno-sanitize-recover=all test_mm_recent.c mymalloc.c pool.c -o test_mm_recent_ubsan
	@./test_mm_recent_ubsan

test-mm: test_mm test_mm_fault
	@mkdir -p results
	@{ ./test_mm && ./test_mm_fault; } > results/test-mm.txt 2>&1; status=$$?; cat results/test-mm.txt; exit $$status

asan-mm: test_mm_asan
	@./test_mm_asan > results/asan-mm.txt 2>&1; status=$$?; cat results/asan-mm.txt; exit $$status

ubsan-mm: test_mm_ubsan
	@./test_mm_ubsan > results/ubsan-mm.txt 2>&1; status=$$?; cat results/ubsan-mm.txt; exit $$status

tsan-mm:
	@sh ./check_mm_tools.sh tsan

bench_mm: bench_mm.c trace_mm.c trace_mm.h check_common.h mymalloc.c mymalloc.h pool.c pool.h
	$(CC) $(CFLAGS) -pthread bench_mm.c trace_mm.c mymalloc.c pool.c -o $@

bench-mm: bench_mm
	@mkdir -p results
	@./bench_mm > results/bench-mm.txt
	@python3 summarize_mm.py results/bench-mm.txt > results/bench-mm-summary.md

memory_mm: memory_mm.c mm_test_alloc.c mm_test_alloc.h mymalloc.c mymalloc.h pool.c pool.h
	$(CC) $(CFLAGS) -include mm_test_alloc.h -Dmalloc=mm_test_malloc -Dfree=mm_test_free -c pool.c -o mm_pool_fault.o
	$(CC) $(CFLAGS) -include mm_test_alloc.h -Dmalloc=mm_test_malloc -Dfree=mm_test_free -c mymalloc.c -o mm_fault.o
	$(CC) $(CFLAGS) memory_mm.c mm_test_alloc.c mm_pool_fault.o mm_fault.o -o $@

memory-mm: memory_mm
	@./memory_mm > results/memory-mm.txt; status=$$?; cat results/memory-mm.txt; exit $$status

clean:
	$(RM) test_pool test_pool_fault test_pool_debug test_pool_asan test_pool_ubsan bench_pool pool_fault.o util_pool util_pool.o slow_check mt_check slow_check.o mt_check.o test_mm test_mm_fault test_mm_asan test_mm_ubsan test_mm_tsan bench_mm memory_mm mm_pool_fault.o mm_fault.o test_mm_recent test_mm_recent_asan test_mm_recent_ubsan
	$(RM) -r .mut *.dSYM __pycache__
	rm -rf codegen
