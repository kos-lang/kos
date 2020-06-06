# SPDX-License-Identifier: MIT
# Copyright (c) 2014-2020 Chris Dragan

depth = interpreter

c_files = kos.c

include ../build/rules.mk

kos_exe = $(out_dir)/kos$(exe_suffix)

LDFLAGS += $(call LD_DEFS,../core/kos_lang)

all: $(kos_exe)

libs =  $(out_dir_base)/modules/$(call LIB_NAME,kos_modules)
libs += $(out_dir_base)/core/$(call LIB_NAME,kos_lang)

$(kos_exe): $(libs)

$(eval $(call LINK_EXE,kos,$(c_files),$(libs)))

install: $(kos_exe) $(wildcard ../modules/*.kos)
	@./install.sh $(out_dir_base) $(destdir)

ifeq ($(UNAME), Windows)
destdir ?= c:/Program\ Files
else
destdir ?= /usr
endif

.PHONY: all install
