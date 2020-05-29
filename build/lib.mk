# SPDX-License-Identifier: MIT
# Copyright (c) 2014-2020 Chris Dragan

all: $(out_dir)/$(depth)/lib$(module)$(a_suffix)

$(out_dir)/$(depth)/lib$(module)$(a_suffix): $(o_files) | $(out_dir)/$(depth)
	@echo Lib $(notdir $@)
	@rm -f $@
ifeq ($(UNAME), Windows)
	@lib -nologo $(LIBFLAGS) $^ -out:$@
else
	@$(AR) rcs $@ $^
endif
