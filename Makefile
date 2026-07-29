CXX ?= clang++
CC ?= clang
GMP_PREFIX ?= /opt/homebrew/opt/gmp
CPPFLAGS ?= -Iinclude -isystem $(GMP_PREFIX)/include
CXXFLAGS ?= -O2 -g -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
CFLAGS ?= -O2 -g -std=c11
LDFLAGS ?= -L$(GMP_PREFIX)/lib
LDLIBS ?= -lgmpxx -lgmp

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
OPENMP_PREFIX ?= /opt/homebrew/opt/libomp
OPENMP_CPPFLAGS ?= -I$(OPENMP_PREFIX)/include -Xpreprocessor -fopenmp
OPENMP_LDFLAGS ?= -L$(OPENMP_PREFIX)/lib -Wl,-rpath,$(OPENMP_PREFIX)/lib -lomp
else
OPENMP_CPPFLAGS ?= -fopenmp
OPENMP_LDFLAGS ?= -fopenmp
endif

BUILD_DIR := build
LIB_SOURCES := src/field.cpp src/poly.cpp src/curve.cpp src/modpoly.cpp src/trace.cpp \
	src/early_abort.cpp src/schoof.cpp src/elkies.cpp src/isogeny.cpp \
	src/smooth_cache.cpp src/factor.cpp
LIB_OBJECTS := $(LIB_SOURCES:src/%.cpp=$(BUILD_DIR)/%.o)

.PHONY: all test test-cli test-reference test-factor test-modpoly-generator test-weber-modpoly test-verifier test-vendor test-smooth test-smooth-cache test-oracle test-differential test-runpod test-all clean

all: $(BUILD_DIR)/oneshotsea

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.cpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/smooth.o: third_party/oneshot_fast_ecpp/smooth.c \
		third_party/oneshot_fast_ecpp/smooth.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -Ithird_party/oneshot_fast_ecpp $(OPENMP_CPPFLAGS) \
		$(CFLAGS) -c $< -o $@

$(BUILD_DIR)/liboneshotsea.a: $(LIB_OBJECTS)
	ar rcs $@ $^

$(BUILD_DIR)/oneshotsea: src/main.cpp $(BUILD_DIR)/liboneshotsea.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD_DIR)/liboneshotsea.a $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_core: tests/test_core.cpp $(BUILD_DIR)/liboneshotsea.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD_DIR)/liboneshotsea.a $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_smooth: tests/test_smooth.c $(BUILD_DIR)/smooth.o
	$(CC) $(CPPFLAGS) -Ithird_party/oneshot_fast_ecpp $(OPENMP_CPPFLAGS) \
		$(CFLAGS) -Wall -Wextra -Werror $^ $(LDFLAGS) $(OPENMP_LDFLAGS) \
		-lgmp -lm -o $@

$(BUILD_DIR)/test_smooth_cache: tests/test_smooth_cache.cpp \
		$(BUILD_DIR)/liboneshotsea.a $(BUILD_DIR)/smooth.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD_DIR)/liboneshotsea.a \
		$(BUILD_DIR)/smooth.o $(LDFLAGS) $(OPENMP_LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_factor: tests/test_factor.cpp $(BUILD_DIR)/liboneshotsea.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD_DIR)/liboneshotsea.a \
		$(LDFLAGS) $(LDLIBS) -o $@

test: $(BUILD_DIR)/test_core
	./$(BUILD_DIR)/test_core

test-cli: all
	python3 tests/test_cli.py -v

test-reference:
	python3 -m unittest -v reference/test_schoof.py reference/test_elkies.py \
		reference/test_elkies_general.py

test-factor: $(BUILD_DIR)/test_factor
	./$(BUILD_DIR)/test_factor

test-modpoly-generator:
	python3 tests/test_modpoly_generator.py -v

test-weber-modpoly:
	python3 tests/test_weber_modpoly.py -v

test-verifier:
	python3 third_party/oneshot_primality_proofs/verify_vendor.py
	python3 third_party/oneshot_primality_proofs/voneshot.py --test

test-vendor:
	python3 third_party/oneshot_fast_ecpp/verify_vendor.py

test-smooth: $(BUILD_DIR)/test_smooth
	./$(BUILD_DIR)/test_smooth

test-smooth-cache: $(BUILD_DIR)/test_smooth_cache
	./$(BUILD_DIR)/test_smooth_cache

test-oracle:
	python3 oracle/test_point_count.py -v

test-differential: all
	python3 tests/test_oracle_differential.py -v

test-runpod:
	scripts/runpod/test.sh

test-all: test test-cli test-reference test-factor test-modpoly-generator test-weber-modpoly test-verifier test-vendor test-smooth test-smooth-cache test-oracle test-differential test-runpod

clean:
	rm -rf $(BUILD_DIR)
