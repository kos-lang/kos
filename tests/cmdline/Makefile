# SPDX-License-Identifier: MIT
# Copyright (c) 2014-2021 Chris Dragan

depth = tests/cmdline

all_test_programs  = kos_true
all_test_programs += kos_false
all_test_programs += kos_cat

c_files = $(addsuffix .c, $(all_test_programs))

include ../../build/rules.mk

$(foreach test, $(all_test_programs), $(eval $(call LINK_EXE,$(test),$(test).c,)))

default: $(addsuffix $(exe_suffix), $(addprefix $(out_dir)/, $(all_test_programs)))
