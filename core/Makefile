# SPDX-License-Identifier: MIT
# Copyright (c) 2014-2021 Chris Dragan

depth = core

c_files  = kos_array.c
c_files += kos_buffer.c
c_files += kos_compiler.c
c_files += kos_compiler_args.c
c_files += kos_compiler_optim.c
c_files += kos_compiler_vars.c
c_files += kos_const_strings.c
c_files += kos_disasm.c
c_files += kos_entity.c
c_files += kos_getline.c
c_files += kos_heap.c
c_files += kos_instance.c
c_files += kos_lexer.c
c_files += kos_malloc.c
c_files += kos_memory.c
c_files += kos_misc.c
c_files += kos_module.c
c_files += kos_object.c
c_files += kos_parser.c
c_files += kos_red_black.c
c_files += kos_stack.c
c_files += kos_string.c
c_files += kos_system.c
c_files += kos_threads.c
c_files += kos_utf8.c
c_files += kos_utils.c
c_files += kos_vm.c

include ../build/rules.mk

$(eval $(call LINK_STATIC_LIB,kos_lang,$(c_files) $(cpp_files)))

default: $(call LIB_TARGET,kos_lang)
