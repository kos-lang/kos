# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: Copyright (c) 2014-2024 Chris Dragan

depth     = build/cldep
cpp_files = cldep.cpp

include ../rules.mk

CL = cl

default: $(out_dir)/cldep$(exe_suffix)

$(eval $(call LINK_EXE,cldep,$(cpp_files),))
