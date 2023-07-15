# SPDX-License-Identifier: MIT
# Copyright (c) 2014-2023 Chris Dragan

depth = interpreter

c_files = kos.c

ifdef tracy
    cpp_files = $(tracy)/TracyClient.cpp
endif

include ../build/rules.mk

kos_exe = $(out_dir)/kos$(exe_suffix)

LDFLAGS += $(call LD_DEFS,../core/kos_lang)

default: $(kos_exe)

libs =  $(out_dir_base)/modules/$(call LIB_NAME,kos_modules)
libs += $(out_dir_base)/core/$(call LIB_NAME,kos_lang)

$(kos_exe): $(libs)

$(eval $(call LINK_EXE,kos,$(c_files) $(cpp_files),$(libs)))

ifdef tracy
    $(call OBJECTS_FROM_SOURCES, $(cpp_files)): | $(dir $(call OBJECTS_FROM_SOURCES, $(cpp_files)))
    $(dir $(call OBJECTS_FROM_SOURCES, $(cpp_files))):
	@mkdir -p $@
endif

install: $(kos_exe) $(wildcard ../modules/*.kos)
	@./install.sh $(out_dir_base) $(destdir)

ifeq ($(UNAME), Windows)
destdir ?= c:/Program\ Files
else
destdir ?= /usr
endif

.PHONY: install
