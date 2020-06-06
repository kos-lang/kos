# SPDX-License-Identifier: MIT
# Copyright (c) 2014-2020 Chris Dragan

depth =

modules  = interpreter
modules += core
modules += modules
modules += tests

include build/rules.mk

# By default, only build the interpreter
all: interpreter

$(modules):
	@$(MAKE) -C $@

clean: clean_gcov

clean_gcov:
	rm -f */*.gcov */*.gcda */*.gcno

interpreter tests: core modules

fuzz: core modules
	@$(MAKE) -C tests fuzz

test: interpreter tests
	@$(MAKE) -C tests $@

cldep:
	@$(MAKE) -C build/cldep debug=0

time_us: core
	@$(MAKE) -C tests/perf/time_us

ifeq ($(UNAME), Windows)
$(modules): cldep
endif

install: interpreter
	@$(MAKE) -C interpreter $@

doc: interpreter
	@echo Extract docs
	@mkdir -p "$(out_dir_base_rel)/doc"
	@env $(out_dir_base_rel)/interpreter/kos$(exe_suffix) doc/extract_docs.kos modules/*.kos modules/*.c > doc/modules.md

defs: interpreter
	@echo Extract defs
	@mkdir -p "$(out_dir_base_rel)/doc"
	@env $(out_dir_base_rel)/interpreter/kos$(exe_suffix) build/extract_defs.kos core/kos_lang inc/*h core/kos_const_strings.h

.PHONY: cldep clean_gcov defs doc install test fuzz time_us $(modules)
