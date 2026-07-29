CXX ?= clang++
GMP_PREFIX ?= /opt/homebrew/opt/gmp
CPPFLAGS ?= -Iinclude -isystem $(GMP_PREFIX)/include
CXXFLAGS ?= -O2 -g -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
LDFLAGS ?= -L$(GMP_PREFIX)/lib
LDLIBS ?= -lgmpxx -lgmp

BUILD_DIR := build
LIB_SOURCES := src/field.cpp src/poly.cpp src/curve.cpp src/modpoly.cpp src/trace.cpp \
	src/early_abort.cpp src/schoof.cpp
LIB_OBJECTS := $(LIB_SOURCES:src/%.cpp=$(BUILD_DIR)/%.o)

.PHONY: all test test-reference test-verifier test-oracle test-differential test-all clean

all: $(BUILD_DIR)/oneshotsea

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.cpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/liboneshotsea.a: $(LIB_OBJECTS)
	ar rcs $@ $^

$(BUILD_DIR)/oneshotsea: src/main.cpp $(BUILD_DIR)/liboneshotsea.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD_DIR)/liboneshotsea.a $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_core: tests/test_core.cpp $(BUILD_DIR)/liboneshotsea.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD_DIR)/liboneshotsea.a $(LDFLAGS) $(LDLIBS) -o $@

test: $(BUILD_DIR)/test_core
	./$(BUILD_DIR)/test_core

test-reference:
	python3 -m unittest -v reference/test_schoof.py

test-verifier:
	python3 third_party/oneshot_primality_proofs/verify_vendor.py
	python3 third_party/oneshot_primality_proofs/voneshot.py --test

test-oracle:
	python3 oracle/test_point_count.py -v

test-differential: all
	python3 tests/test_oracle_differential.py -v

test-all: test test-reference test-verifier test-oracle test-differential

clean:
	rm -rf $(BUILD_DIR)
