# SPDX-License-Identifier: MIT
# Copyright (c) 2014-2020 Chris Dragan

module = kos
depth  = interpreter

c_files = kos.c

include ../build/rules.mk

all: $(out_dir)/interpreter/kos$(exe_suffix)

$(out_dir)/interpreter/kos$(exe_suffix): $(o_files) $(out_dir)/modules/libkos_modules$(a_suffix) $(out_dir)/core/libkos_lang$(a_suffix)
	$(call LINK,$@,$^)
	@echo Exe $(@:../%=%)

install: $(out_dir)/interpreter/kos$(exe_suffix) $(wildcard ../modules/*.kos)
	@./install.sh $(out_dir) $(destdir)

ifeq ($(UNAME), Windows)
destdir ?= c:/Program\ Files
else
destdir ?= /usr
endif

.PHONY: all install
