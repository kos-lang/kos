# SPDX-License-Identifier: MIT
# Copyright (c) 2014-2020 Chris Dragan

all: $(addprefix build., $(modules))

clean: $(addprefix clean., $(modules))

build.%:
	@$(MAKE) -C $(@:build.%=%)

clean.%:
	@$(MAKE) -C $(@:clean.%=%) clean

.PHONY: all clean
