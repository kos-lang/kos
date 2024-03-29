# SPDX-License-Identifier: MIT
# Copyright (c) 2014-2023 Chris Dragan

depth = tests

standalone_tests  = kos_array_test
standalone_tests += kos_atomic_test
standalone_tests += kos_basic_object_test
standalone_tests += kos_buffer_test
standalone_tests += kos_convert_test
standalone_tests += kos_cpp_api_test
standalone_tests += kos_gc_test
standalone_tests += kos_iterator_test
standalone_tests += kos_mempool_test
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

cmd_line_programs  = kos_true
cmd_line_programs += kos_false
cmd_line_programs += kos_cat

all_test_programs  = $(standalone_tests)
all_test_programs += kos_parser_test
all_test_programs += kos_print_heap_test
all_test_programs += kos_module_path_test
all_test_programs += $(cmd_line_programs)

fuzz_tests  = kos_fuzz_compiler
fuzz_tests += kos_fuzz_interpreter

c_files  = kos_array_test.c
c_files += kos_atomic_test.c
c_files += kos_basic_object_test.c
c_files += kos_buffer_test.c
c_files += kos_convert_test.c
c_files += kos_fuzz_compiler.c
c_files += kos_fuzz_interpreter.c
c_files += kos_gc_test.c
c_files += kos_iterator_test.c
c_files += kos_mempool_test.c
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
$(out_dir)/kos_vm_unit_test$(o_suffix): CFLAGS += -Wno-missing-field-initializers
endif
endif

default: $(addsuffix $(exe_suffix), $(addprefix $(out_dir)/, $(all_test_programs)))

tool ?=

libs =  $(out_dir_base)/modules/$(call LIB_NAME,kos_modules)
libs += $(out_dir_base)/core/$(call LIB_NAME,kos_lang)

LINK_TEST = $(call LINK_EXE,$1,$1.c,$(libs))

$(foreach test, $(all_test_programs), $(eval $(call LINK_TEST,$(test))))

define PARALLEL_DEP
$(out_dir)/$1$(exe_suffix): $(out_dir)/kos_test_tools$(o_suffix)
endef

$(foreach test, $(filter kos_parallel_%, $(all_test_programs)), $(eval $(call PARALLEL_DEP,$(test))))

parser_test_list = $(wildcard parser_tests/*.kos)

define PARSER_TEST
$1:
	@echo Test kos_parser_test $(notdir $1)
	@$(tool) $(out_dir)/kos_parser_test$(exe_suffix) $1
endef

$(foreach test, $(parser_test_list), $(eval $(call PARSER_TEST,$(test))))

interpreter_test_list = $(filter-out interpreter_tests/fail_% interpreter_tests/module_base_print.kos, $(wildcard interpreter_tests/*.kos))

define INTERPRETER_TEST
$1:
	@echo Test kos $(notdir $1)
	@env $(tool) $(out_dir_base)/interpreter/kos$(exe_suffix) $1
endef

$(foreach test, $(interpreter_test_list), $(eval $(call INTERPRETER_TEST,$(test))))

all_interpreter_fail_test_list = $(filter interpreter_tests/fail_%, $(wildcard interpreter_tests/*.kos))

exclude_mad_gc_list =

ifeq ($(mad_gc), 1)
exclude_mad_gc_list = fail_oom_5.kos
endif

interpreter_fail_test_list = $(filter-out $(addprefix interpreter_tests/, $(exclude_mad_gc_list)), $(all_interpreter_fail_test_list))

define INTERPRETER_FAIL_TEST
$1:
	@echo Test kos $(notdir $1)
	@env interpreter_tests/fail $(out_dir)/$1 $(tool) $(out_dir_base)/interpreter/kos$(exe_suffix) $1
endef

$(foreach test, $(interpreter_fail_test_list), $(eval $(call INTERPRETER_FAIL_TEST,$(test))))

define RUN_STANDALONE_TEST
$1:
	@echo Test $1
	@$(tool) $(out_dir)/$1$(exe_suffix)
endef

$(foreach test, $(standalone_tests), $(eval $(call RUN_STANDALONE_TEST,$(test))))

fuzz: $(addsuffix $(exe_suffix), $(addprefix $(out_dir)/, $(fuzz_tests)))

$(foreach test, $(fuzz_tests), $(eval $(call LINK_TEST,$(test))))

interactive_scripts  = disasm
interactive_scripts += interactive
interactive_scripts += module_base_print
interactive_scripts += module_loading
interactive_scripts += module_os_spawn

$(interactive_scripts):
	@echo Test $@
	@interpreter_tests/$@ $(out_dir_base)/interpreter/kos$(exe_suffix) $(out_dir)

module_paths:
	@echo Test $@
	@./$@ $(out_dir)/kos_module_path_test$(exe_suffix) $(out_dir_base)/interpreter/kos$(exe_suffix)

print_heap_test:
	@echo Test $@
	@./$@ $(out_dir)/kos_print_heap_test$(exe_suffix) $(out_dir)

pseudotty_scripts =  interactive
ifneq ($(shell uname -m),s390x) # signal tests hang on IBM Z in Travis
pseudotty_scripts += sigwinch
pseudotty_scripts += sigstop
endif

pseudotty = $(out_dir)/pseudotty/pseudotty
$(pseudotty)::
	@$(MAKE) -C pseudotty

interpreter_tests/module_os.kos: $(addsuffix $(exe_suffix), $(addprefix $(out_dir)/, $(cmd_line_programs)))

define PSEUDOTTY_RULE
pseudotty_$1: $(pseudotty) pseudotty/$1
	@echo Test pseudotty $1
	@pseudotty/run_pseudotty $(pseudotty) $(out_dir_base)/interpreter/kos$(exe_suffix) pseudotty/$1 $(out_dir_base)/tests/pseudotty/$(basename $1)
endef

$(foreach test, $(pseudotty_scripts), $(eval $(call PSEUDOTTY_RULE,$(test))))

ifeq (,$(filter Linux Darwin %BSD, $(UNAME)))
    pseudotty_test_list =
else
    pseudotty_test_list = $(addprefix pseudotty_, $(pseudotty_scripts))
    default: $(pseudotty)
endif

all_tests = $(standalone_tests) $(parser_test_list) $(interactive_scripts) module_paths print_heap_test $(pseudotty_test_list) $(interpreter_test_list) $(interpreter_fail_test_list)

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
	@env sequential_fail/sequential_fail $(out_dir_base)/interpreter/kos$(exe_suffix) $(out_dir) $1 200

sequential_test_list += sequential_fail.$1
endef

last_seq_fail_point = 7600

$(foreach seq_fail_point, $(shell seq 0 200 $(last_seq_fail_point)), $(eval $(call SEQFAIL_RULE,$(seq_fail_point))))

ifneq ($(seqfail), 0)
simple_test: $(sequential_test_list)
endif

define GCOV_RULE
gcov.$1: simple_test
	@echo Gcov $1
	@rm -f ../$1/*.gcov
	@cd ../$1 && $(GCOV) --object-directory $(out_dir_base)/$1 *.c* > /dev/null
	@rm -f ../core/kos.h.gcov ../core/kos_cpp.h.gcov # gcov does not find anything in these files
endef

$(foreach module, $(gcov_modules), $(eval $(call GCOV_RULE,$(module))))

lcov ?= 0

ifeq ($(lcov), 0)
gcov: $(addprefix gcov., $(gcov_modules))
	@find $(out_dir_base)/ -name "*.gcno" -o -name "*.gcda" -exec rm '{}' +
else
gcov: simple_test $(all_tests)
	@echo Lcov kos.info
	@find $(out_dir)/ -name "*.gcno" -o -name "*.gcda" -exec rm '{}' +
	@lcov --capture --directory .. --output-file $(out_dir_base)/kos.info
	@find $(out_dir_base)/ -name "*.gcno" -o -name "*.gcda" -exec rm '{}' +
endif

gcov.pre.clean:
	@find $(out_dir_base)/ -name "*.gcno" -o -name "*.gcda" -exec rm '{}' +

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

.PHONY: test simple_test gcov fuzz
.PHONY: $(addprefix gcov., $(gcov_modules))
.PHONY: $(standalone_tests) $(parser_test_list) $(sequential_test_list) $(interactive_scripts)
.PHONY: module_paths print_heap_test
.PHONY: $(addprefix pseudotty_, $(pseudotty_scripts))
.PHONY: $(interpreter_test_list) $(interpreter_fail_test_list)
