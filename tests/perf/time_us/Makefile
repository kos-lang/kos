# SPDX-License-Identifier: MIT
# Copyright (c) 2014-2022 Chris Dragan

depth = tests/perf/time_us

c_files = time_us.c

include ../../../build/rules.mk

default: $(out_dir)/time_us$(exe_suffix)

libs = $(out_dir_base)/core/$(call LIB_NAME,kos_lang)

$(eval $(call LINK_EXE,time_us,$(c_files),$(libs)))
