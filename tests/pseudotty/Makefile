# SPDX-License-Identifier: MIT
# Copyright (c) 2014-2020 Chris Dragan

depth = tests/pseudotty

c_files = pseudotty.c

include ../../build/rules.mk

default: $(out_dir)/pseudotty$(exe_suffix)

$(eval $(call LINK_EXE,pseudotty,$(c_files),))
