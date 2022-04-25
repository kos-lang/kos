# SPDX-License-Identifier: MIT
# Copyright (c) 2014-2022 Chris Dragan

depth =

modules  = interpreter
modules += core
modules += modules
modules += tests

include build/rules.mk

# By default, only build the interpreter
default: interpreter modules_ext

$(modules):
	@$(MAKE) -C $@

modules_ext:
	@$(MAKE) -C modules external

clean: clean_gcov

clean_gcov:
	rm -f */*.gcov */*.gcda */*.gcno

interpreter tests: core modules

fuzz: core modules
	@$(MAKE) -C tests fuzz

test: default tests
	@$(MAKE) -C tests $@

cldep:
	@$(MAKE) -C build/cldep debug=0

time_us: core
	@$(MAKE) -C tests/perf/time_us

ifeq ($(UNAME), Windows)
$(modules): cldep
modules_ext: interpreter
endif

install: default
	@$(MAKE) -C interpreter $@

doc: default
	@echo Extract docs
	@env $(out_dir_base_rel)/interpreter/kos$(exe_suffix) doc/extract_docs.kos modules/*.kos modules/*.c > doc/modules.md

defs: default
	@echo Extract defs
	@env $(out_dir_base_rel)/interpreter/kos$(exe_suffix) build/extract_defs.kos core/kos_lang inc/*h modules/*h

.PHONY: cldep clean_gcov defs doc install modules_ext test fuzz time_us $(modules)
