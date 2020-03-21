# SPDX-License-Identifier: MIT
# Copyright (c) 2014-2020 Chris Dragan

module    = cldep
depth     = build/cldep
cpp_files = cldep.cpp

out_dir ?= $(call inv_path, $(depth))/Out/cldep

include ../rules.mk

CL = cl

all: $(out_dir)/build/cldep/cldep$(exe_suffix)

$(out_dir)/build/cldep/cldep$(exe_suffix): $(out_dir)/build/cldep/cldep$(o_suffix)
	$(call LINK,$@,$^)

.PHONY: all
