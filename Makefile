# SPDX-License-Identifier: MIT
# Copyright (c) 2014-2020 Chris Dragan

modules  = interpreter
modules += core
modules += modules
modules += tests

# By default, only build the interpreter
build.interpreter:

include build/dirs.mk

debug ?= 0

ifeq ($(debug), 0)
    out_dir ?= Out/release
else
    out_dir ?= Out/debug
endif

clean:
	rm -rf $(out_dir)
	rm -f */*.gcov */*.gcda */*.gcno

build.interpreter build.tests: build.core build.modules

fuzz: build.core build.modules
	@$(MAKE) -C tests fuzz

test: build.interpreter build.tests
	@$(MAKE) -C tests $@

cldep:
	@$(MAKE) -C build/cldep debug=0

time_us: build.core
	@$(MAKE) -C tests/perf/time_us

ifneq (,$(filter CYGWIN% MINGW% MSYS%, $(shell uname -s)))
$(addprefix build., $(modules)): cldep
endif

install: build.interpreter
	@$(MAKE) -C interpreter $@

doc: build.interpreter
	@echo Extract docs
	@mkdir -p "$(out_dir)/doc"
	@env $(out_dir)/interpreter/kos$(exe_suffix) doc/extract_docs.kos modules/*.kos modules/*.c > doc/modules.md

.PHONY: cldep doc install test fuzz time_us
