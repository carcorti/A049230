CC = gcc
CPPFLAGS =
CFLAGS = -O3 -flto -funroll-loops -fomit-frame-pointer -march=native \
	-mtune=native -std=c17 -Wall -Wextra -Wpedantic -Wshadow -Wconversion \
	-Wdouble-promotion -Wformat=2
LDFLAGS =
LDLIBS = -lm
OPENMP = -fopenmp

PROGRAM := a049230
SOURCE := a049230.c

define checked_sha256
$(strip $(shell digest=$$(sha256sum -- "$(1)" 2>/dev/null) || exit 1; \
	digest=$${digest%% *}; test "$${#digest}" -eq 64 || exit 1; \
	printf '%s\n' "$$digest" | grep -Eq '^[0-9a-f]{64}$$' || exit 1; \
	printf '%s' "$$digest"))
endef

SOURCE_SHA := $(call checked_sha256,$(SOURCE))
MAKEFILE_SHA := $(call checked_sha256,Makefile)
ifeq ($(strip $(SOURCE_SHA)),)
$(error Cannot compute SHA-256 for $(SOURCE); sha256sum is required)
endif
ifeq ($(strip $(MAKEFILE_SHA)),)
$(error Cannot compute SHA-256 for Makefile; sha256sum is required)
endif
IDENTITY_FLAGS := -DA049230_SOURCE_SHA=\"$(SOURCE_SHA)\" -DA049230_MAKEFILE_SHA=\"$(MAKEFILE_SHA)\"

.PHONY: all clean test

all: $(PROGRAM)

$(PROGRAM): $(SOURCE) Makefile
	$(CC) $(CPPFLAGS) $(IDENTITY_FLAGS) $(CFLAGS) $(OPENMP) $(SOURCE) $(LDFLAGS) $(OPENMP) $(LDLIBS) -o $@

test: $(PROGRAM)
	@set -eu; test_root=$$(mktemp -d /tmp/a049230-make-test.XXXXXX); \
	trap 'case "$$test_root" in /tmp/a049230-make-test.*) rm -rf -- "$$test_root";; esac' EXIT HUP INT TERM; \
	printf '%s\n' '1 0' '2 0' '3 0' '4 0' '5 4' '6 36' '7 227' '8 1312' > "$$test_root/expected.tsv"; \
	./$(PROGRAM) compute --max-n 8 --threads 1 --split-depth 8 > "$$test_root/monolithic.tsv"; \
	cmp "$$test_root/expected.tsv" "$$test_root/monolithic.tsv"; \
	printf '%s\n' '999999999999999999999999 0' > "$$test_root/oversized.tsv"; \
	if ./$(PROGRAM) compute --max-n 1 --threads 1 --split-depth 1 --p3-file "$$test_root/oversized.tsv" > /dev/null 2>&1; then \
		printf '%s\n' 'oversized oracle field was accepted' >&2; exit 1; \
	fi; \
	if ./$(PROGRAM) compute --max-n 1 --threads 1 --split-depth 1 > /dev/full 2>/dev/null; then \
		printf '%s\n' 'standard-output failure was not reported' >&2; exit 1; \
	fi; \
	if ./$(PROGRAM) init --state "$$test_root/output-fail" --max-n 8 --threads 1 --split-depth 8 --unit-size 64 --segment-seconds 1 > /dev/full 2>/dev/null; then \
		printf '%s\n' 'initialization output failure was not reported' >&2; exit 1; \
	fi; \
	test -f "$$test_root/output-fail/manifest.tsv"; \
	./$(PROGRAM) init --state "$$test_root/run" --max-n 8 --threads 1 --split-depth 8 --unit-size 64 --segment-seconds 1; \
	./$(PROGRAM) next --state "$$test_root/run"; \
	./$(PROGRAM) verify --state "$$test_root/run"; \
	if ./$(PROGRAM) status --state "$$test_root/run" > /dev/full 2>/dev/null; then \
		printf '%s\n' 'status output failure was not reported' >&2; exit 1; \
	fi; \
	cmp "$$test_root/expected.tsv" "$$test_root/run/results.tsv"; \
	./$(PROGRAM) init --state "$$test_root/finalization/" --max-n 8 --threads 1 --split-depth 8 --unit-size 64 --segment-seconds 1 > /dev/null; \
	mkdir "$$test_root/finalization/results.tmp"; \
	if ./$(PROGRAM) next --state "$$test_root/finalization/" > /dev/null 2>&1; then \
		printf '%s\n' 'obstructed finalization unexpectedly succeeded' >&2; exit 1; \
	fi; \
	test ! -e "$$test_root/finalization/results.tsv"; \
	./$(PROGRAM) status --state "$$test_root/finalization/" > "$$test_root/pending-status.tsv"; \
	grep -q '^state[[:space:]]*finalization_pending$$' "$$test_root/pending-status.tsv"; \
	rmdir "$$test_root/finalization/results.tmp"; \
	./$(PROGRAM) next --state "$$test_root/finalization/" > /dev/null; \
	./$(PROGRAM) status --state "$$test_root/finalization/" > "$$test_root/complete-status.tsv"; \
	grep -q '^state[[:space:]]*complete$$' "$$test_root/complete-status.tsv"; \
	./$(PROGRAM) verify --state "$$test_root/finalization/"; \
	cmp "$$test_root/expected.tsv" "$$test_root/finalization/results.tsv"; \
	printf '%s\n' 'Makefile test: PASS'

clean:
	$(RM) $(PROGRAM)
