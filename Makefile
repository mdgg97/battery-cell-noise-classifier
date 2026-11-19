CC      ?= gcc
CSTD    := -std=c99
WARN    := -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror
CFLAGS  ?= -O2
CFLAGS  += $(CSTD) $(WARN) -Iinclude
BUILD   := build

SRC     := src/cell_noise.c
UNIT    := tests/unit/test_cell_noise.c

.PHONY: all lib unit sil test coverage memcheck sanitize optlevels lint trace divcount \
        arm qemu arm-report clean

# ---- Cortex-M4 target (QEMU mps2-an386) ------------------------------------
ARM_CC    ?= arm-none-eabi-gcc
ARM_SIZE  ?= arm-none-eabi-size
ARM_ARCH  := -mcpu=cortex-m4 -mthumb -mfloat-abi=soft
ARM_BUILD := $(BUILD)/arm
QEMU      ?= qemu-system-arm

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

test: unit sil divcount trace lint

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

# REQ-034: number of 64-bit divisions per window must not grow with the window length
divcount:
	python3 tools/count_divisions.py

# Library alone, freestanding (no libc headers), as it would go into firmware
$(ARM_BUILD)/cell_noise.o: $(SRC) include/cell_noise.h
	mkdir -p $(ARM_BUILD)
	$(ARM_CC) $(ARM_ARCH) $(CSTD) $(WARN) -Os -ffreestanding -fstack-usage -Iinclude -c $(SRC) -o $@

# Unit tests linked for the target, output via semihosting
$(ARM_BUILD)/unit_tests.elf: $(SRC) $(UNIT) target/startup.c target/mps2_an386.ld
	mkdir -p $(ARM_BUILD)
	$(ARM_CC) $(ARM_ARCH) $(CSTD) -O2 -g -Wall -Wextra -Werror -Iinclude -Itests/unit \
	  -nostartfiles -T target/mps2_an386.ld --specs=rdimon.specs \
	  target/startup.c $(SRC) $(UNIT) -o $@ -Wl,--gc-sections

arm: $(ARM_BUILD)/cell_noise.o $(ARM_BUILD)/unit_tests.elf

# Run the C unit tests on an emulated Cortex-M4
qemu: $(ARM_BUILD)/unit_tests.elf
	timeout 120 $(QEMU) -machine mps2-an386 -nographic -monitor none -serial none \
	  -semihosting-config enable=on,target=native -kernel $< | tee $(ARM_BUILD)/qemu.log
	grep -q " 0 failed" $(ARM_BUILD)/qemu.log

# Flash / RAM / stack of the library on the target
arm-report: $(ARM_BUILD)/cell_noise.o
	@echo "Library size on Cortex-M4 (-Os):"
	@$(ARM_SIZE) $<
	@echo "Stack usage per function (bytes):"
	@cat $(ARM_BUILD)/cell_noise.su

clean:
	rm -rf $(BUILD)
