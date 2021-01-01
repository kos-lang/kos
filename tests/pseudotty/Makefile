# SPDX-License-Identifier: MIT
# Copyright (c) 2014-2021 Chris Dragan

depth = tests/pseudotty

c_files = pseudotty.c

include ../../build/rules.mk

default: $(out_dir)/pseudotty$(exe_suffix)

ifeq ($(UNAME), Linux)
CFLAGS += -D_XOPEN_SOURCE=600
endif

$(eval $(call LINK_EXE,pseudotty,$(c_files),))
