#
# Copyright (c) 2014-2017 Chris Dragan
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to
# deal in the Software without restriction, including without limitation the
# rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
# sell copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.
#

##############################################################################
# Determine target OS

UNAME := $(shell uname -s)

ifneq (,$(filter CYGWIN% MINGW% MSYS%, $(UNAME)))
    # Note: Still use cl.exe on Windows
    UNAME := Windows
endif

##############################################################################
# Determine language version for source files

ifeq ($(UNAME), Windows)
    CLANG   = -TC
    CPPLANG = -TP -EHsc
else
    CLANG   = -x c
    CPPLANG = -x c++
    CLANG_VER   ?=
    CPPLANG_VER ?=
    ifneq (,$(CLANG_VER))
        CLANG += -std=$(CLANG_VER)
    endif
    ifneq (,$(CPPLANG_VER))
        CPPLANG += -std=$(CPPLANG_VER)
    endif
endif

ifneq (,$(filter c++%, $(CLANG_VER)))
    CLANG = $(CPPLANG)
endif

##############################################################################
# Function to convert a relative path to a reverse path

empty :=
space := $(empty) $(empty)
inv_path = $(subst $(space),/,$(patsubst %,..,$(subst /,$(space),$1)))

##############################################################################
# Configure debug and optimization options

CFLAGS  ?=
LDFLAGS ?=
CONFIG_DEBUG ?= 0
CONFIG_NATIVE ?= 0
ifeq ($(UNAME), Windows)
    LIBFLAGS ?=
    ifeq ($(CONFIG_DEBUG), 0)
        CFLAGS   += -O2 -DNDEBUG -Gs4096 -GL -MT
        LDFLAGS  += -LTCG
        LIBFLAGS += -LTCG
    else
        CFLAGS  += -Z7 -MTd
        LDFLAGS += -DEBUG
    endif

    STRICTFLAGS = -WX

    # TODO remove this when all functions are implemented
    STRICTFLAGS += -wd4100 # unreferenced formal parameter

    # Disable warnings which don't make any sense for this project
    CFLAGS += -wd4061 # enumerator 'x' in switch of enum 'y' is not explicitly handled by a case label
    CFLAGS += -wd4464 # relative path contains '..'
    CFLAGS += -wd4514 # unreferenced inline function has been removed
    CFLAGS += -wd4571 # catch(...): structured exceptions (SEH) are no longer caught
    CFLAGS += -wd4625 # copy constructor was implicitly defined as deleted
    CFLAGS += -wd4626 # assignment operator was implicitly defined as deleted
    CFLAGS += -wd4710 # 'snprintf': function not inlined
    CFLAGS += -wd4711 # function 'x' selected for automatic inline expansion
    CFLAGS += -wd4820 # 'x' bytes padding added after data member 'y'
    CFLAGS += -wd5026 # move constructor was implicitly defined as deleted
    CFLAGS += -wd5027 # move assignment operator was implicitly defined as deleted

    CONFIG_GCOV := 0
else
    ifeq ($(CONFIG_DEBUG), 0)
        CFLAGS += -O3 -DNDEBUG -ffunction-sections -fdata-sections
        STRIP  += strip
        ifeq ($(UNAME), Linux)
            LDFLAGS += -Wl,--gc-sections -Wl,--as-needed
        endif
        ifeq ($(UNAME), Darwin)
            LDFLAGS += -Wl,-dead_strip
        endif
        ifeq ($(CONFIG_NATIVE), 1)
            CFLAGS  += -march=native
            LDFLAGS += -march=native
        endif

        # Configure LTO, if available
        ifeq ($(UNAME), Linux)
            LTOAR ?= $(shell $(call inv_path, $(depth))/build/find_lto ar $(CC))
            ifeq (,$(LTOAR))
                CONFIG_LTO ?= 0
            else
                CONFIG_LTO ?= 1
            endif
            ifneq ($(CONFIG_LTO), 0)
                AR =  $(LTOAR)
            endif
        endif
        ifeq ($(UNAME), Darwin)
            CONFIG_LTO ?= 1
        endif
        CONFIG_LTO ?= 0
        ifneq ($(CONFIG_LTO), 0)
            CFLAGS  += -flto
            LDFLAGS += -ffunction-sections -fdata-sections -flto
        endif
    else
        CFLAGS += -O0 -g
    endif

    STRICTFLAGS = -Wextra -Werror

    # TODO remove this when all functions are implemented
    STRICTFLAGS += -Wno-unused-parameter

    ifeq ($(UNAME), Linux)
        CFLAGS  += -D_BSD_SOURCE -D_DEFAULT_SOURCE
        LDFLAGS += -lpthread
    endif

    # Configure gcov
    CONFIG_GCOV ?= 0
    ifneq ($(CONFIG_GCOV), 0)
        CFLAGS  += -fprofile-arcs -ftest-coverage -fno-inline
        LDFLAGS += -fprofile-arcs -ftest-coverage
    endif
endif

CONFIG_PERF ?= 0

ifneq ($(CONFIG_PERF), 0)
    CFLAGS += -DCONFIG_PERF
endif

##############################################################################
# Optionally treat warnings as errors and add more checks

CONFIG_STRICT ?= 0 # Old compilers don't like it, disable by default

ifeq ($(CONFIG_STRICT), 1)
    CFLAGS += $(STRICTFLAGS)
endif

##############################################################################
# Set default output directory

ifeq ($(CONFIG_DEBUG), 0)
    out_dir ?= $(call inv_path, $(depth))/Out/release
else
    out_dir ?= $(call inv_path, $(depth))/Out/debug
endif

##############################################################################
# Filenames of object files and dependency files

ifeq ($(UNAME), Windows)
    o_suffix   := .obj
    a_suffix   := .lib
    exe_suffix := .exe
else
    o_suffix   := .o
    a_suffix   := .a
    exe_suffix :=
endif

cpp_files ?=
c_files   ?=
o_files = $(addprefix $(out_dir)/$(depth)/, $(c_files:.c=$(o_suffix)) $(cpp_files:.cpp=$(o_suffix)))
d_files = $(addprefix $(out_dir)/$(depth)/, $(c_files:.c=.d) $(cpp_files:.cpp=.d))

##############################################################################
# Declare 'all' as the first/default rule

all:

##############################################################################
# Include dependency files

-include $(d_files)

##############################################################################
# Clean rule

clean:
	rm -rf $(out_dir)/$(depth)

##############################################################################
# C and C++ rules

ifeq ($(UNAME), Windows)

CL = $(out_dir)/../cldep/build/cldep/cldep$(exe_suffix)

$(out_dir)/$(depth)/$(notdir %.obj): %.c
ifeq (,$(filter c++%, $(CLANG_VER)))
	@echo C $(notdir $@)
else
	@echo C++ $(notdir $@)
endif
	@mkdir -p $(dir $@)
	@$(CL) -nologo -Wall $(CLANG)   $(CFLAGS) -Fo$@ -c $<

$(out_dir)/$(depth)/$(notdir %.obj): %.cpp
	@echo C++ $(notdir $@)
	@mkdir -p $(dir $@)
	@$(CL) -nologo -Wall $(CPPLANG) $(CFLAGS) -Fo$@ -c $<

else #------------------------------------------------------------------------

$(out_dir)/$(depth)/$(notdir %.o): %.c
ifeq (,$(filter c++%, $(CLANG_VER)))
	@echo C $(notdir $@)
else
	@echo C++ $(notdir $@)
endif
	@mkdir -p $(dir $@)
	@$(CC) -Wall -pedantic $(CFLAGS) -c -MD $(CLANG)   $< -o $@
	@test -f $(<:.c=.d)   && mv $(<:.c=.d)   $(dir $@) || true # WAR for very old gcc

$(out_dir)/$(depth)/$(notdir %.o): %.cpp
	@echo C++ $(notdir $@)
	@mkdir -p $(dir $@)
	@$(CC) -Wall -pedantic $(CFLAGS) -c -MD $(CPPLANG) $< -o $@
	@test -f $(<:.cpp=.d) && mv $(<:.cpp=.d) $(dir $@) || true # WAR for very old gcc

endif

##############################################################################
# Link rule

ifeq ($(UNAME), Windows)

define LINK
	@basename $1 | xargs echo Link
	@link -nologo $(LDFLAGS) -out:$1 $2
endef

else #------------------------------------------------------------------------

ifeq ($(CONFIG_DEBUG), 0)
define LINK
	@basename $1 | xargs echo Link
	@$(CXX) $2 -o $1 $(LDFLAGS)
	@$(STRIP) $1
endef
else
define LINK
	@basename $1 | xargs echo Link
	@$(CXX) $2 -o $1 $(LDFLAGS)
endef
endif

endif

##############################################################################
# Declare virtual targets

.PHONY: all clean
