CC      ?= gcc
CSTD    := -std=c99
WARN    := -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror
CFLAGS  ?= -O2
CFLAGS  += $(CSTD) $(WARN) -Iinclude
BUILD   := build

SRC     := src/cell_noise.c
UNIT    := tests/unit/test_cell_noise.c

.PHONY: all lib unit sil test coverage memcheck sanitize optlevels lint trace clean

all: lib $(BUILD)/unit_tests

lib: $(BUILD)/libcellnoise.so

$(BUILD):
	mkdir -p $(BUILD)

# Shared library for the pytest software-in-the-loop tests (ctypes)
$(BUILD)/libcellnoise.so: $(SRC) include/cell_noise.h | $(BUILD)
	$(CC) $(CFLAGS) -fPIC -shared $(SRC) -o $@

$(BUILD)/unit_tests: $(SRC) $(UNIT) include/cell_noise.h | $(BUILD)
	$(CC) $(CFLAGS) -Itests/unit $(SRC) $(UNIT) -o $@

unit: $(BUILD)/unit_tests
	./$(BUILD)/unit_tests

sil: lib
	python3 -m pytest tests/sil -q

test: unit sil trace lint

# Line/branch coverage of the C code from the unit tests (gcov)
coverage: | $(BUILD)
	$(CC) $(CSTD) -O0 -g --coverage -Iinclude -Itests/unit $(SRC) $(UNIT) -o $(BUILD)/unit_cov
	./$(BUILD)/unit_cov > /dev/null
	gcov -b -o $(BUILD) $(BUILD)/unit_cov-cell_noise.gcda | grep -A4 "File 'src/cell_noise.c'"
	@mv -f *.gcov $(BUILD)/ 2>/dev/null; echo "annotated source: $(BUILD)/cell_noise.c.gcov"

memcheck: $(BUILD)/unit_tests
	valgrind --error-exitcode=1 --leak-check=full -q ./$(BUILD)/unit_tests > /dev/null

# Undefined behaviour / memory errors that may only show up under optimisation
sanitize: | $(BUILD)
	$(CC) $(CSTD) $(WARN) -O2 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
	  -Iinclude -Itests/unit $(SRC) $(UNIT) -o $(BUILD)/unit_san
	./$(BUILD)/unit_san > /dev/null && echo "sanitize: ASan + UBSan clean"

# Same results at every optimisation level the firmware might be built with
optlevels: | $(BUILD)
	@for O in -O0 -O1 -O2 -O3 -Os; do \
	  $(CC) $(CSTD) $(WARN) $$O -Iinclude -Itests/unit $(SRC) $(UNIT) -o $(BUILD)/unit$$O && \
	  ./$(BUILD)/unit$$O > /dev/null && echo "optlevels: $$O ok" || exit 1; \
	done

# REQ-030: no heap and no floating point in production code
lint:
	@! grep -nE '\b(malloc|calloc|realloc|free|float|double)\b' $(SRC) include/cell_noise.h \
	  || (echo "REQ-030 violated" && false)
	@echo "lint: no heap / float usage in src (REQ-030)"

trace:
	python3 tools/trace_matrix.py

clean:
	rm -rf $(BUILD)
