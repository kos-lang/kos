#
# Copyright (c) 2014-2018 Chris Dragan
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

modules  = interpreter
modules += core
modules += modules
modules += tests

# By default, only build the interpreter
build.interpreter:

include build/dirs.mk

CONFIG_DEBUG ?= 0

ifeq ($(CONFIG_DEBUG), 0)
    out_dir ?= Out/release
else
    out_dir ?= Out/debug
endif

clean:
	rm -rf $(out_dir)
	rm -f */*.gcov */*.gcda */*.gcno

build.interpreter build.tests: build.core build.modules

test: build.interpreter build.tests
	@$(MAKE) -C tests $@

cldep:
	@$(MAKE) -C build/cldep CONFIG_DEBUG=0

ifneq (,$(filter CYGWIN% MINGW% MSYS%, $(shell uname -s)))
$(addprefix build., $(modules)): cldep
endif

install: build.interpreter
	@$(MAKE) -C interpreter $@

doc: build.interpreter
	@echo Extract docs
	@mkdir -p "$(out_dir)/doc"
	@env KOSPATH=modules $(out_dir)/interpreter/kos$(exe_suffix) doc/extract_docs.kos modules/*.kos modules/*.c > doc/modules.md

.PHONY: cldep doc install test
