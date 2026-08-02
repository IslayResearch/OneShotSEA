CXX ?= clang++
CC ?= clang
GMP_PREFIX ?= /opt/homebrew/opt/gmp
CPPFLAGS ?= -Iinclude -isystem $(GMP_PREFIX)/include
CXXFLAGS ?= -O2 -g -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
CFLAGS ?= -O2 -g -std=c11
LDFLAGS ?= -L$(GMP_PREFIX)/lib
LDLIBS ?= -lgmpxx -lgmp

.DEFAULT_GOAL := all

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
LIB_SOURCES := src/field.cpp src/poly.cpp src/curve.cpp src/modpoly.cpp src/trace.cpp src/atkin.cpp src/weber_table_trust.cpp \
	src/early_abort.cpp src/schoof.cpp src/elkies.cpp src/isogeny.cpp src/weber.cpp src/sea.cpp \
	src/smooth_cache.cpp src/smooth_bounded.cpp src/integrity.cpp src/exact_smooth.cpp src/factor.cpp src/search_checkpoint.cpp src/certificate.cpp \
	src/weber_curve_generator.cpp src/x1_11_probe.cpp src/x1_27_probe.cpp \
	src/search_pipeline.cpp
LIB_OBJECTS := $(LIB_SOURCES:src/%.cpp=$(BUILD_DIR)/%.o)
LIB_DEPS := $(LIB_OBJECTS:.o=.d)

-include $(LIB_DEPS)

.PHONY: all test test-cli test-reference test-factor test-certificate test-eigenvalue-mitm test-modpoly-generator test-weber-modpoly test-weber-curve-generator test-verifier test-vendor test-smooth test-smooth-cache test-exact-smooth test-search-checkpoint test-search-pipeline test-poly-reduction benchmark-poly-reduction test-oracle test-differential test-runpod test-all clean

all: $(BUILD_DIR)/oneshotsea

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.cpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/smooth_bounded.o: src/smooth_bounded.cpp \
		include/oneshotsea/smooth_bounded.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(OPENMP_CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/smooth.o: third_party/oneshot_fast_ecpp/smooth.c \
		third_party/oneshot_fast_ecpp/smooth.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -Ithird_party/oneshot_fast_ecpp $(OPENMP_CPPFLAGS) \
		$(CFLAGS) -c $< -o $@

$(BUILD_DIR)/liboneshotsea.a: $(LIB_OBJECTS)
	ar rcs $@ $^

$(BUILD_DIR)/oneshotsea: src/main.cpp $(BUILD_DIR)/liboneshotsea.a \
		$(BUILD_DIR)/smooth.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD_DIR)/liboneshotsea.a \
		$(BUILD_DIR)/smooth.o $(LDFLAGS) $(OPENMP_LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_core: tests/test_core.cpp $(BUILD_DIR)/liboneshotsea.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD_DIR)/liboneshotsea.a $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_poly_reduction: tests/test_poly_reduction.cpp \
		$(BUILD_DIR)/liboneshotsea.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD_DIR)/liboneshotsea.a \
		$(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_smooth: tests/test_smooth.c $(BUILD_DIR)/smooth.o
	$(CC) $(CPPFLAGS) -Ithird_party/oneshot_fast_ecpp $(OPENMP_CPPFLAGS) \
		$(CFLAGS) -Wall -Wextra -Werror $^ $(LDFLAGS) $(OPENMP_LDFLAGS) \
		-lgmp -lm -o $@

$(BUILD_DIR)/test_smooth_cache: tests/test_smooth_cache.cpp \
		$(BUILD_DIR)/liboneshotsea.a $(BUILD_DIR)/smooth.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD_DIR)/liboneshotsea.a \
		$(BUILD_DIR)/smooth.o $(LDFLAGS) $(OPENMP_LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_exact_smooth: tests/test_exact_smooth.cpp \
		$(BUILD_DIR)/liboneshotsea.a $(BUILD_DIR)/smooth.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD_DIR)/liboneshotsea.a \
		$(BUILD_DIR)/smooth.o $(LDFLAGS) $(OPENMP_LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_factor: tests/test_factor.cpp $(BUILD_DIR)/liboneshotsea.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD_DIR)/liboneshotsea.a \
		$(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_atkin: tests/test_atkin.cpp $(BUILD_DIR)/liboneshotsea.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD_DIR)/liboneshotsea.a \
		$(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_eigenvalue_mitm: tests/test_eigenvalue_mitm.cpp \
		$(BUILD_DIR)/liboneshotsea.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD_DIR)/liboneshotsea.a \
		$(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_certificate: tests/test_certificate.cpp \
		$(BUILD_DIR)/liboneshotsea.a $(BUILD_DIR)/smooth.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD_DIR)/liboneshotsea.a \
		$(BUILD_DIR)/smooth.o $(LDFLAGS) $(OPENMP_LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_search_checkpoint: tests/test_search_checkpoint.cpp \
		$(BUILD_DIR)/liboneshotsea.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD_DIR)/liboneshotsea.a \
		$(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_search_pipeline: tests/test_search_pipeline.cpp \
		$(BUILD_DIR)/liboneshotsea.a $(BUILD_DIR)/smooth.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD_DIR)/liboneshotsea.a \
		$(BUILD_DIR)/smooth.o $(LDFLAGS) $(OPENMP_LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_weber_curve_generator: \
		tests/test_weber_curve_generator.cpp $(BUILD_DIR)/liboneshotsea.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD_DIR)/liboneshotsea.a \
		$(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/oracle_weber_audit: \
		oracle/weber_audit.cpp $(BUILD_DIR)/liboneshotsea.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD_DIR)/liboneshotsea.a \
		$(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_x1_11_probe: \
		tests/test_x1_11_probe.cpp $(BUILD_DIR)/liboneshotsea.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD_DIR)/liboneshotsea.a \
		$(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_x1_27_probe: \
		tests/test_x1_27_probe.cpp $(BUILD_DIR)/liboneshotsea.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD_DIR)/liboneshotsea.a \
		$(LDFLAGS) $(LDLIBS) -o $@

test: $(BUILD_DIR)/test_core
	./$(BUILD_DIR)/test_core

test-poly-reduction: $(BUILD_DIR)/test_poly_reduction
	./$(BUILD_DIR)/test_poly_reduction

benchmark-poly-reduction: $(BUILD_DIR)/test_poly_reduction
	./$(BUILD_DIR)/test_poly_reduction --bench

test-cli: all
	python3 tests/test_cli.py -v

test-reference:
	python3 -m unittest -v reference/test_schoof.py reference/test_elkies.py \
		reference/test_elkies_general.py

test-factor: $(BUILD_DIR)/test_factor
	./$(BUILD_DIR)/test_factor

test-eigenvalue-mitm: $(BUILD_DIR)/test_eigenvalue_mitm
	./$(BUILD_DIR)/test_eigenvalue_mitm

test-certificate: $(BUILD_DIR)/test_certificate
	./$(BUILD_DIR)/test_certificate

test-modpoly-generator:
	python3 tests/test_modpoly_generator.py -v

test-weber-modpoly:
	python3 tests/test_weber_modpoly.py -v

test-weber-curve-generator: $(BUILD_DIR)/test_weber_curve_generator
	./$(BUILD_DIR)/test_weber_curve_generator

test-weber-audit: $(BUILD_DIR)/oracle_weber_audit
	./$(BUILD_DIR)/oracle_weber_audit --p 101 --seed 17 --range-start 0 \
		--count 1 --max-level 5 --trace-cap 4 --sea-threads 1 \
		--table-dir data/modpoly/weber_f --schoof-fallback 1 | \
		python3 -c 'import json,sys; rows=[json.loads(line) for line in sys.stdin]; assert len(rows)==1; row=rows[0]; p=int(row["p"]); trace=int(row["final_exact_trace"]); d=int(row["twist_parameter"]); curve={k:int(v) for k,v in row["curve"].items()}; twist={k:int(v) for k,v in row["twist"].items()}; discriminant=lambda side:(4*pow(side["a"],3,p)+27*pow(side["b"],2,p))%p; invariant=lambda side:(1728*4*pow(side["a"],3,p)*pow(discriminant(side),-1,p))%p; point_trace=lambda side:p+1-(1+sum(1+(0 if (rhs:=(pow(x,3,p)+side["a"]*x+side["b"])%p)==0 else 1 if pow(rhs,(p-1)//2,p)==1 else -1) for x in range(p))); assert row["schema"]=="oneshotsea.weber-audit.v1" and row["index"]=="0"; assert row["complete"] and row["final_exact_only"] and not row["smoothness_audited"]; assert row["final"]["exact_trace_candidate_count"]=="1"; assert int(row["early"]["trace_count"])==len(row["early"]["traces"]); assert all(level["classification"] in {"exact_elkies","certified_atkin","unconstrained"} for level in row["early"]["levels"]+row["final"]["levels"]); assert all(item["trace_residues"] for item in row["early"]["atkin_constraints"]+row["final"]["atkin_constraints"]); assert isinstance(row["final"]["exact_residue_classes"],list) and isinstance(row["final"]["effective_residue_classes"],list); assert pow(d,(p-1)//2,p)==p-1 and all(pow(candidate,(p-1)//2,p)!=p-1 for candidate in range(2,d)); assert twist["a"]==curve["a"]*pow(d,2,p)%p and twist["b"]==curve["b"]*pow(d,3,p)%p; assert discriminant(curve)!=0 and discriminant(twist)!=0; assert invariant(curve)==invariant(twist)==int(row["j"]); assert point_trace(curve)==trace and point_trace(twist)==-trace'
	./$(BUILD_DIR)/oracle_weber_audit --p 101 --seed 17 --range-start 0 \
		--count 1 --max-level 5 --trace-cap 4 --sea-threads 1 \
		--table-dir data/modpoly/weber_f --schoof-fallback 0 | \
		python3 -c 'import json,sys; rows=[json.loads(line) for line in sys.stdin]; assert len(rows)==1; row=rows[0]; assert not row["complete"] and not row["final_exact_only"]; assert row["unique_mode"]=="fresh_cap_one" and row["final_exact_trace"] is None; assert row["final"]["status"]=="level_limit" and row["final"]["traces"] is None; assert row["early"]["fallback_levels"]==row["final"]["fallback_levels"]==[]'

test-weber-corpus: $(BUILD_DIR)/oracle_weber_audit
	python3 tests/test_weber_corpus_audit.py -v

test-weber-early-abort-audit:
	python3 tests/test_weber_early_abort_audit.py -v

test-x1-11-probe: $(BUILD_DIR)/test_x1_11_probe
	./$(BUILD_DIR)/test_x1_11_probe

test-x1-27-probe: $(BUILD_DIR)/test_x1_27_probe
	./$(BUILD_DIR)/test_x1_27_probe

test-verifier:
	python3 third_party/oneshot_primality_proofs/verify_vendor.py
	python3 third_party/oneshot_primality_proofs/voneshot.py --test

test-vendor:
	python3 third_party/oneshot_fast_ecpp/verify_vendor.py

test-smooth: $(BUILD_DIR)/test_smooth
	./$(BUILD_DIR)/test_smooth

test-smooth-cache: $(BUILD_DIR)/test_smooth_cache
	./$(BUILD_DIR)/test_smooth_cache

test-exact-smooth: $(BUILD_DIR)/test_exact_smooth
	./$(BUILD_DIR)/test_exact_smooth

test-search-checkpoint: $(BUILD_DIR)/test_search_checkpoint
	./$(BUILD_DIR)/test_search_checkpoint

test-search-pipeline: $(BUILD_DIR)/test_search_pipeline
	./$(BUILD_DIR)/test_search_pipeline

test-oracle:
	python3 oracle/test_point_count.py -v

test-oracle-corpus:
	python3 tests/test_oracle_corpus_audit.py -v

test-differential: all
	python3 tests/test_oracle_differential.py -v

test-runpod: all
	scripts/runpod/test.sh

test-all: test test-poly-reduction test-cli test-reference test-factor test-certificate test-eigenvalue-mitm test-modpoly-generator test-weber-modpoly test-weber-curve-generator test-verifier test-vendor test-smooth test-smooth-cache test-exact-smooth test-search-checkpoint test-search-pipeline test-oracle test-differential test-runpod

clean:
	rm -rf $(BUILD_DIR)
