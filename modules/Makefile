# SPDX-License-Identifier: MIT
# Copyright (c) 2014-2020 Chris Dragan

depth = modules

c_files  = kos_mod_base.c
c_files += kos_mod_datetime.c
c_files += kos_mod_fs.c
c_files += kos_mod_io.c
c_files += kos_mod_kos.c
c_files += kos_mod_random.c
c_files += kos_modules_init.c

include ../build/rules.mk

######################################################################
# Module shared libraries

builtin_modules  ?= 1
external_modules ?=

ifeq ($(builtin_modules), 1)
    define EXTERNAL_MODULE
        c_files += $$(c_files_$1)
    endef
else
    CFLAGS += -DKOS_EXTERNAL_MODULES
    SHARED_LDFLAGS += $(call LD_DEFS,kos_module)
    ifeq ($(UNAME), Windows)
        SHARED_LDFLAGS += $(out_dir_base)/interpreter/kos.lib
    endif
    define EXTERNAL_MODULE
        external_modules += $1
        -include $$(call DEPS_FROM_SOURCES,$$(c_files_$1))
        $$(call OBJECTS_FROM_SOURCES,$$(c_files_$1)): CFLAGS += -DKOS_PUBLIC_API
        $$(eval $$(call LINK_SHARED_LIB,$1,$$(c_files_$1),))
    endef
endif

c_files_math = kos_mod_math.c
$(eval $(call EXTERNAL_MODULE,math))

c_files_os = kos_mod_os.c
$(eval $(call EXTERNAL_MODULE,os))

c_files_re = kos_mod_re.c
$(eval $(call EXTERNAL_MODULE,re))

######################################################################
# Link built-in modules

$(eval $(call LINK_STATIC_LIB,kos_modules,$(c_files) $(cpp_files)))

default: $(call LIB_TARGET,kos_modules)

######################################################################
# Copy modules for the interpreter

modules = $(wildcard *.kos)

ifeq ($(UNAME), Windows)
modules_dir = $(out_dir_base)/interpreter/modules
else
modules_dir = $(out_dir_base)/share/kos/modules
endif

default: copy_modules

.PHONY: copy_modules external

copy_modules: $(addprefix $(modules_dir)/,$(modules))

$(modules_dir):
	@mkdir -p $(modules_dir)

define COPY_MODULE
$(modules_dir)/$1: $1 | $(modules_dir)
	@cp $$< $$@
endef

$(foreach module, $(modules), $(eval $(call COPY_MODULE,$(module))))

define COPY_NATIVE_MODULE
external: $(modules_dir)/$1$(so_suffix)
$(modules_dir)/$1$(so_suffix): $(call SHARED_TARGET,$1) | $(modules_dir)
	@cp $$< $$@
endef

$(foreach module, $(external_modules), $(eval $(call COPY_NATIVE_MODULE,$(module))))
