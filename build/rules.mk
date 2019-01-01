#
# Copyright (c) 2014-2019 Chris Dragan
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
CFLAGS += -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=1
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
    STRICTFLAGS += -wd4100 # unreferenced formal parameter

    # Disable warnings which don't make any sense for this project
    CFLAGS += -wd4061 # enumerator 'x' in switch of enum 'y' is not explicitly handled by a case label
    CFLAGS += -wd4365 # conversion from 'x' to 'y' (in MS headers)
    CFLAGS += -wd4464 # relative path contains '..'
    CFLAGS += -wd4514 # unreferenced inline function has been removed
    CFLAGS += -wd4571 # catch(...): structured exceptions (SEH) are no longer caught
    CFLAGS += -wd4623 # __std_type_info_data': default constructor was implicitly defined as deleted (in MS headers)
    CFLAGS += -wd4625 # copy constructor was implicitly defined as deleted
    CFLAGS += -wd4626 # assignment operator was implicitly defined as deleted
    CFLAGS += -wd4668 # _M_HYBRID_X86_ARM64 is not defined (in MS headers)
    CFLAGS += -wd4710 # 'snprintf': function not inlined
    CFLAGS += -wd4711 # function 'x' selected for automatic inline expansion
    CFLAGS += -wd4774 # _scprintf' : format string expected in argument 1 is not a string literal (in MS headers)
    CFLAGS += -wd4820 # 'x' bytes padding added after data member 'y'
    CFLAGS += -wd4987 # nonstandard extension used (in MS headers)
    CFLAGS += -wd5026 # move constructor was implicitly defined as deleted
    CFLAGS += -wd5027 # move assignment operator was implicitly defined as deleted
    CFLAGS += -wd5039 # pointer or reference to potentially throwing function passed to extern C function under -EHc
    CFLAGS += -wd5045 # compiler will insert Spectre mitigation for memory load if /Qspectre switch specified

    CONFIG_GCOV := 0
else
    ifeq ($(CONFIG_DEBUG), 0)
        CFLAGS += -O3 -DNDEBUG -ffunction-sections -fdata-sections
        STRIP  ?= strip
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
    STRICTFLAGS += -Wno-unused-parameter
    STRICTFLAGS += -Wshadow
    STRICTFLAGS += -Wunused
    #STRICTFLAGS += -Wnull-dereference
    STRICTFLAGS += -Wdouble-promotion
    STRICTFLAGS += -Wformat=2
    #STRICTFLAGS += -Wconversion

    ifeq ($(UNAME), Linux)
        CFLAGS  += -D_BSD_SOURCE -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200112L
        LDFLAGS += -lpthread -lrt
    endif

    ifeq ($(UNAME), Darwin)
        CFLAGS  += -DCONFIG_EDITLINE
        LDFLAGS += -ledit -ltermcap
    else
        ifeq (true,$(shell $(call inv_path, $(depth))/build/have_readline $(CC)))
            CFLAGS  += -DCONFIG_READLINE
            LDFLAGS += -lreadline
        endif
    endif

    ifneq (,$(filter FreeBSD OpenBSD NetBSD DragonFly,$(UNAME)))
        LDFLAGS += -lpthread
    endif

    # Configure gcov
    CONFIG_GCOV ?= 0
    ifneq ($(CONFIG_GCOV), 0)
        CFLAGS  += -fprofile-arcs -ftest-coverage -fno-inline
        LDFLAGS += -fprofile-arcs -ftest-coverage
    endif

    # Special handling of fuzzer
    CONFIG_FUZZ ?= 0
    ifneq ($(CONFIG_FUZZ), 0)
        CFLAGS += -DCONFIG_FUZZ
    endif
endif

ifeq ($(UNAME), Darwin)
    CFLAGS  += -mmacosx-version-min=10.9
    LDFLAGS += -mmacosx-version-min=10.9
endif

CONFIG_PERF ?= 0

ifneq ($(CONFIG_PERF), 0)
    CFLAGS += -DCONFIG_PERF
endif

CONFIG_STRING ?= 8

ifeq ($(CONFIG_STRING), 16)
    CFLAGS += -DCONFIG_STRING16
endif
ifeq ($(CONFIG_STRING), 32)
    CFLAGS += -DCONFIG_STRING32
endif

CONFIG_SEQFAIL ?= 0

ifneq ($(CONFIG_SEQFAIL), 0)
    CFLAGS += -DCONFIG_SEQFAIL
endif

CONFIG_MAD_GC ?= 0

ifneq ($(CONFIG_MAD_GC), 0)
    CFLAGS += -DCONFIG_MAD_GC
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
