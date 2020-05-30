# SPDX-License-Identifier: MIT
# Copyright (c) 2014-2020 Chris Dragan

depth     = build/cldep
cpp_files = cldep.cpp

include ../rules.mk

CL = cl

all: $(out_dir)/cldep$(exe_suffix)

$(eval $(call LINK_EXE,cldep,$(cpp_files)))

.PHONY: all
