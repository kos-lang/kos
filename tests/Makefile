#
# Copyright (c) 2014-2020 Chris Dragan
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to
# deal in the Software without restriction, including without limitation the
# rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
# sell copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.
#

module = tests
depth  = tests

standalone_tests  = kos_array_test
standalone_tests += kos_atomic_test
standalone_tests += kos_basic_object_test
standalone_tests += kos_buffer_test
standalone_tests += kos_cpp_api_test
standalone_tests += kos_gc_test
standalone_tests += kos_module_load_test
standalone_tests += kos_object_alloc_test
standalone_tests += kos_object_test
standalone_tests += kos_random_test
standalone_tests += kos_red_black_test
standalone_tests += kos_parallel_array_resize_test
standalone_tests += kos_parallel_buffer_test
standalone_tests += kos_parallel_instance_destroy
standalone_tests += kos_parallel_object_consistency_test
standalone_tests += kos_parallel_object_overwrite_test
standalone_tests += kos_parallel_object_rapid_grow_test
standalone_tests += kos_parallel_object_resize_test
standalone_tests += kos_parse_num_test
standalone_tests += kos_string_test
standalone_tests += kos_utf8_len
standalone_tests += kos_vm_test
standalone_tests += kos_vm_unit_test

all_tests  = $(standalone_tests)
all_tests += kos_parser_test
all_tests += kos_print_heap_test
all_tests += kos_module_path_test

fuzz_tests  = kos_fuzz_compiler
fuzz_tests += kos_fuzz_interpreter

c_files  = kos_array_test.c
c_files += kos_atomic_test.c
c_files += kos_basic_object_test.c
c_files += kos_buffer_test.c
c_files += kos_fuzz_compiler.c
c_files += kos_fuzz_interpreter.c
c_files += kos_gc_test.c
c_files += kos_module_load_test.c
c_files += kos_module_path_test.c
c_files += kos_object_alloc_test.c
c_files += kos_object_test.c
c_files += kos_random_test.c
c_files += kos_red_black_test.c
c_files += kos_parallel_array_resize_test.c
c_files += kos_parallel_buffer_test.c
c_files += kos_parallel_instance_destroy.c
c_files += kos_parallel_object_consistency_test.c
c_files += kos_parallel_object_overwrite_test.c
c_files += kos_parallel_object_rapid_grow_test.c
c_files += kos_parallel_object_resize_test.c
c_files += kos_parse_num_test.c
c_files += kos_parser_test.c
c_files += kos_print_heap_test.c
c_files += kos_string_test.c
c_files += kos_test_tools.c
c_files += kos_vm_test.c
c_files += kos_vm_unit_test.c

cpp_files += kos_cpp_api_test.cpp

gcov_modules  = interpreter
gcov_modules += core
gcov_modules += modules

GCOV ?= gcov

include ../build/rules.mk

ifneq ($(UNAME), Windows)
ifeq ($(strict), 1)
$(out_dir)/$(depth)/kos_vm_unit_test$(o_suffix): CFLAGS += -Wno-missing-field-initializers
endif
endif

all: $(addsuffix $(exe_suffix), $(addprefix $(out_dir)/tests/, $(all_tests)))

tool ?=

define LINK_TEST
$(out_dir)/tests/$1$(exe_suffix): $(out_dir)/tests/$1$(o_suffix) $(out_dir)/modules/libkos_modules$(a_suffix) $(out_dir)/core/libkos_lang$(a_suffix)
	$(call LINK,$$@,$$^)
endef

$(foreach test, $(all_tests), $(eval $(call LINK_TEST,$(test))))

define PARALLEL_DEP
$(out_dir)/tests/$1$(exe_suffix): $(out_dir)/tests/kos_test_tools$(o_suffix)
endef

$(foreach test, $(filter kos_parallel_%, $(all_tests)), $(eval $(call PARALLEL_DEP,$(test))))

parser_test_list = $(wildcard parser_tests/*.kos)

define PARSER_TEST
$1:
	@echo Test kos_parser_test $(notdir $1)
	@$(tool) $(out_dir)/tests/kos_parser_test$(exe_suffix) $1
endef

$(foreach test, $(parser_test_list), $(eval $(call PARSER_TEST,$(test))))

interpreter_test_list = $(filter-out interpreter_tests/fail_% interpreter_tests/module_base_print.kos, $(wildcard interpreter_tests/*.kos))

define INTERPRETER_TEST
$1:
	@echo Test kos $(notdir $1)
	@env $(tool) $(out_dir)/interpreter/kos$(exe_suffix) $1
endef

$(foreach test, $(interpreter_test_list), $(eval $(call INTERPRETER_TEST,$(test))))

interpreter_fail_test_list = $(filter interpreter_tests/fail_%, $(wildcard interpreter_tests/*.kos))

define INTERPRETER_FAIL_TEST
$1:
	@echo Test kos $(notdir $1)
	@env interpreter_tests/fail $(out_dir)/tests/$1 $(tool) $(out_dir)/interpreter/kos$(exe_suffix) $1
endef

$(foreach test, $(interpreter_fail_test_list), $(eval $(call INTERPRETER_FAIL_TEST,$(test))))

define RUN_STANDALONE_TEST
$1:
	@echo Test $1
	@$(tool) $(out_dir)/tests/$1$(exe_suffix)
endef

$(foreach test, $(standalone_tests), $(eval $(call RUN_STANDALONE_TEST,$(test))))

fuzz: $(addsuffix $(exe_suffix), $(addprefix $(out_dir)/tests/, $(fuzz_tests)))

$(foreach test, $(fuzz_tests), $(eval $(call LINK_TEST,$(test))))

interactive_scripts  = disasm
interactive_scripts += interactive
interactive_scripts += module_base_print
interactive_scripts += module_loading

$(interactive_scripts):
	@echo Test $@
	@interpreter_tests/$@ $(out_dir)/interpreter/kos$(exe_suffix) $(out_dir)/tests

module_paths:
	@echo Test $@
	@./$@ $(out_dir)/tests/kos_module_path_test$(exe_suffix) $(out_dir)/interpreter/kos$(exe_suffix)

print_heap_test:
	@echo Test $@
	@./$@ $(out_dir)/tests/kos_print_heap_test$(exe_suffix) $(out_dir)/tests

all_tests = $(standalone_tests) $(parser_test_list) $(interactive_scripts) module_paths print_heap_test $(interpreter_test_list) $(interpreter_fail_test_list)

all_simple_tests = $(filter-out kos_parallel%, $(all_tests))

# Ignore some tests with Valgrind
ifeq ($(tool),)
    simple_tests = $(all_simple_tests)
else
    simple_tests = $(filter-out interpreter_tests/async_throw.kos, $(all_simple_tests))
endif

simple_test: $(simple_tests)

sequential_test_list =

define SEQFAIL_RULE
sequential_fail.$1:
	@echo Test sequential fail $1
	@env sequential_fail/sequential_fail $(out_dir)/interpreter/kos$(exe_suffix) $(out_dir)/tests $1 200

sequential_test_list += sequential_fail.$1
endef

$(eval $(call SEQFAIL_RULE,0))
$(eval $(call SEQFAIL_RULE,200))
$(eval $(call SEQFAIL_RULE,400))
$(eval $(call SEQFAIL_RULE,600))
$(eval $(call SEQFAIL_RULE,800))
$(eval $(call SEQFAIL_RULE,1000))
$(eval $(call SEQFAIL_RULE,1200))
$(eval $(call SEQFAIL_RULE,1400))
$(eval $(call SEQFAIL_RULE,1600))
$(eval $(call SEQFAIL_RULE,1800))
$(eval $(call SEQFAIL_RULE,2000))
$(eval $(call SEQFAIL_RULE,2200))
$(eval $(call SEQFAIL_RULE,2400))
$(eval $(call SEQFAIL_RULE,2600))
$(eval $(call SEQFAIL_RULE,2800))
$(eval $(call SEQFAIL_RULE,3000))
$(eval $(call SEQFAIL_RULE,3200))
$(eval $(call SEQFAIL_RULE,3400))
$(eval $(call SEQFAIL_RULE,3600))
$(eval $(call SEQFAIL_RULE,3800))
$(eval $(call SEQFAIL_RULE,4000))
$(eval $(call SEQFAIL_RULE,4200))
$(eval $(call SEQFAIL_RULE,4400))
$(eval $(call SEQFAIL_RULE,4600))
$(eval $(call SEQFAIL_RULE,4800))
$(eval $(call SEQFAIL_RULE,5000))
$(eval $(call SEQFAIL_RULE,5200))
$(eval $(call SEQFAIL_RULE,5400))
$(eval $(call SEQFAIL_RULE,5600))
$(eval $(call SEQFAIL_RULE,5800))

ifneq ($(seqfail), 0)
simple_test: $(sequential_test_list)
endif

define GCOV_RULE
gcov.$1: simple_test
	@echo Gcov $1
	@rm -f ../$1/*.gcov
	@cd ../$1 && $(GCOV) --object-directory $(out_dir)/$1 *.c* > /dev/null
	@rm -f ../core/kos.h.gcov # gcov does not find anything in this file
endef

$(foreach module, $(gcov_modules), $(eval $(call GCOV_RULE,$(module))))

gcov: $(addprefix gcov., $(gcov_modules))
	@find $(out_dir)/ -name "*.gcno" -o -name "*.gcda" -exec rm '{}' +

gcov.pre.clean:
	@find $(out_dir)/ -name "*.gcno" -o -name "*.gcda" -exec rm '{}' +

# Execute parallel tests last, make them depend on each other so that they are executed alone
# Don't run parallel tests with valgrind, since they are slow

ifeq ($(tool),)
# Normal testing
kos_parallel_instance_destroy:        simple_test
kos_parallel_object_consistency_test: kos_parallel_instance_destroy
kos_parallel_object_resize_test:      kos_parallel_object_consistency_test
kos_parallel_object_overwrite_test:   kos_parallel_object_resize_test
kos_parallel_object_rapid_grow_test:  kos_parallel_object_overwrite_test
kos_parallel_buffer_test:             kos_parallel_object_rapid_grow_test
kos_parallel_array_resize_test:       kos_parallel_buffer_test
test:                                 kos_parallel_array_resize_test
else
# Valgrind
test:                                 simple_test
endif

ifneq ($(gcov), 0)
test: gcov

$(all_tests): gcov.pre.clean

$(addprefix gcov., $(gcov_modules)): $(all_tests)
endif

.PHONY: all test simple_test gcov fuzz $(addprefix gcov., $(gcov_modules)) $(standalone_tests) $(parser_test_list) $(sequential_test_list) $(interactive_scripts) module_paths print_heap_test $(interpreter_test_list) $(interpreter_fail_test_list)
