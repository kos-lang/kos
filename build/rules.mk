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
    CLANG       = -x c
    CPPLANG     = -x c++
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
debug   ?= 0
native  ?= 0
target  ?=
ifeq ($(UNAME), Windows)
    LIBFLAGS ?=
    ifeq ($(debug), 0)
        CFLAGS   += -O2 -DNDEBUG -Gs4096 -GL -MT
        LDFLAGS  += -LTCG
        LIBFLAGS += -LTCG
    else
        CFLAGS  += -Z7 -MTd
        LDFLAGS += -DEBUG
    endif

    STRICTFLAGS = -WX

    # Disable warnings which don't make any sense for this project
    CFLAGS += -wd4061 # enumerator 'x' in switch of enum 'y' is not explicitly handled by a case label
    CFLAGS += -wd4100 # unreferenced formal parameter
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

    gcov := 0
else
    ifeq ($(debug), 0)
        CFLAGS += -O3 -DNDEBUG -ffunction-sections -fdata-sections
        CFLAGS += -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=1
        STRIP  ?= strip
        ifeq ($(UNAME), Linux)
            LDFLAGS += -Wl,--gc-sections -Wl,--as-needed
        endif
        ifeq ($(UNAME), Darwin)
            LDFLAGS += -Wl,-dead_strip
        endif
        ifeq ($(native), 1)
            CFLAGS  += -march=native
            LDFLAGS += -march=native
        endif

        # Configure LTO, if available
        ifeq ($(UNAME), Linux)
            LTOAR ?= $(shell $(call inv_path, $(depth))/build/find_lto ar $(CC))
            ifeq (,$(LTOAR))
                lto ?= 0
            else
                lto ?= 1
            endif
            ifneq ($(lto), 0)
                AR =  $(LTOAR)
            endif
        endif
        ifeq ($(UNAME), Darwin)
            lto ?= 1
        endif
        lto ?= 0
        ifneq ($(lto), 0)
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

    fastdispatch ?= 1
    ifeq ($(fastdispatch), 1)
        COMPILER_VER := $(shell $(CC) --version 2>&1)
        ifeq (,$(filter clang gcc,$(COMPILER_VER)))
            fastdispatch := 0
        endif
    endif
    ifeq ($(fastdispatch), 1)
        CFLAGS += -DCONFIG_FAST_DISPATCH
    else
        STRICTFLAGS += -pedantic
    endif

    ifeq ($(UNAME), Linux)
        CFLAGS  += -D_BSD_SOURCE -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200112L
        LDFLAGS += -lpthread -lrt
    endif

    ifeq ($(UNAME), Darwin)
        ifneq ($(target), ios)
            CFLAGS  += -DCONFIG_EDITLINE
            LDFLAGS += -ledit -ltermcap
        endif
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
    gcov ?= 0
    ifneq ($(gcov), 0)
        CFLAGS  += -fprofile-arcs -ftest-coverage -fno-inline
        LDFLAGS += -fprofile-arcs -ftest-coverage
    endif

    # Special handling of fuzzer
    fuzz ?= 0
    ifneq ($(fuzz), 0)
        CFLAGS += -DCONFIG_FUZZ
    endif
endif

ifeq ($(UNAME), Darwin)
    ifeq ($(target), ios)
        APPLE_FLAGS ?= -mios-version-min=7.0 -arch arm64 -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk
    else
        APPLE_FLAGS ?= -mmacosx-version-min=10.9
    endif
    CFLAGS  += $(APPLE_FLAGS)
    LDFLAGS += $(APPLE_FLAGS)
endif

##############################################################################
# Various options

threads ?= 1

ifneq ($(threads), 1)
    CFLAGS += -DCONFIG_THREADS=0
endif

perf ?= 0

ifneq ($(perf), 0)
    CFLAGS += -DCONFIG_PERF
endif

min_string_size ?= 8

ifeq ($(min_string_size), 16)
    CFLAGS += -DCONFIG_STRING16
endif
ifeq ($(min_string_size), 32)
    CFLAGS += -DCONFIG_STRING32
endif

seqfail ?= 0

ifneq ($(seqfail), 0)
    CFLAGS += -DCONFIG_SEQFAIL
endif

mad_gc ?= 0

ifneq ($(mad_gc), 0)
    CFLAGS += -DCONFIG_MAD_GC
endif

##############################################################################
# Optionally treat warnings as errors and add more checks

strict ?= 0 # Old compilers don't like it, disable by default

ifeq ($(strict), 1)
    CFLAGS += $(STRICTFLAGS)
endif

##############################################################################
# Set default output directory

ifeq ($(debug), 0)
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
	@$(CC) -Wall $(CFLAGS) -c -MD $(CLANG)   $< -o $@
	@test -f $(<:.c=.d)   && mv $(<:.c=.d)   $(dir $@) || true # WAR for very old gcc

$(out_dir)/$(depth)/$(notdir %.o): %.cpp
	@echo C++ $(notdir $@)
	@mkdir -p $(dir $@)
	@$(CC) -Wall $(CFLAGS) -c -MD $(CPPLANG) $< -o $@
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

ifeq ($(debug), 0)
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
