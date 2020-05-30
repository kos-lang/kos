# SPDX-License-Identifier: MIT
# Copyright (c) 2014-2020 Chris Dragan

depth = modules

c_files  = kos_mod_base.c
c_files += kos_mod_datetime.c
c_files += kos_mod_fs.c
c_files += kos_mod_io.c
c_files += kos_mod_kos.c
c_files += kos_mod_math.c
c_files += kos_mod_random.c
c_files += kos_mod_re.c
c_files += kos_modules_init.c

include ../build/rules.mk

$(eval $(call LINK_STATIC_LIB,kos_modules,$(c_files) $(cpp_files)))

all: $(call LIB_TARGET,kos_modules)

######################################################################
# Copy modules for the interpreter

modules=$(wildcard *.kos)

ifeq ($(UNAME), Windows)
modules_dir=$(out_dir_base)/interpreter/modules
else
modules_dir=$(out_dir_base)/share/kos/modules
endif

all: $(addprefix $(modules_dir)/,$(modules))

$(modules_dir):
	@mkdir -p $(modules_dir)

define COPY_MODULE
$(modules_dir)/$1: $1 | $(modules_dir)
	@cp $1 $(modules_dir)/$1
endef

$(foreach module, $(modules), $(eval $(call COPY_MODULE,$(module))))
