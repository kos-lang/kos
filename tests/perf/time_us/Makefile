# SPDX-License-Identifier: MIT
# Copyright (c) 2014-2020 Chris Dragan

module = kos
depth  = tests/perf/time_us

c_files = time_us.c

include ../../../build/rules.mk

all: $(out_dir)/$(depth)/time_us$(exe_suffix)

$(out_dir)/$(depth)/time_us$(exe_suffix): $(o_files) $(out_dir)/core/libkos_lang$(a_suffix)
	$(call LINK,$@,$^)
	@echo Exe $(@:../../../%=%)
