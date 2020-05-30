# SPDX-License-Identifier: MIT
# Copyright (c) 2014-2020 Chris Dragan

depth = tests/perf/time_us

c_files = time_us.c

include ../../../build/rules.mk

all: $(out_dir)/time_us$(exe_suffix)

libs = $(out_dir_base)/core/libkos_lang$(a_suffix)

$(eval $(call LINK_EXE,time_us,$(c_files),$(libs)))
