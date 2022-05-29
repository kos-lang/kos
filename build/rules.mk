# SPDX-License-Identifier: MIT
# Copyright (c) 2014-2022 Chris Dragan

##############################################################################
# Function to convert a relative path to a reverse path

empty :=
space := $(empty) $(empty)
inv_path = $(if $1,$(subst $(space),/,$(patsubst %,..,$(subst /,$(space),$1)))/,)

##############################################################################
# Set default output directory

debug ?= 0

ifeq ($(debug), 0)
    out_dir_base_rel = Out/release
else
    out_dir_base_rel = Out/debug
endif

target ?=
ifneq ($(target),)
    out_dir_base_rel := $(out_dir_base_rel)-$(target)
endif

kos_dir     ?= $(call inv_path,$(depth))
out_dir_base = $(kos_dir)$(out_dir_base_rel)
out_dir_rel  = $(out_dir_base_rel)/$(depth)
out_dir      = $(out_dir_base)/$(depth)

##############################################################################
# Determine target OS

UNAME = $(shell uname -s)

ifneq (,$(filter CYGWIN% MINGW% MSYS%, $(UNAME)))
    # Note: Still use cl.exe on Windows
    UNAME = Windows
endif

##############################################################################
# Determine language version for source files

ifdef tracy
    CPPLANG_VER ?= c++11
    CLANG_VER   ?= $(CPPLANG_VER)
endif

CLANG_VER   ?=
CPPLANG_VER ?=

ifeq ($(UNAME), Windows)
    CLANG   = -TC
    CPPLANG = -TP -EHsc
    VSCPPLANG_VER = $(filter-out c89 c99 c11 c++98 c++11,$(CPPLANG_VER))
    ifneq (,$(VSCPPLANG_VER))
        CPPLANG += -std:$(VSCPPLANG_VER) -Zc:__cplusplus
    endif
else
    CLANG   = -x c
    CPPLANG = -x c++
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
# Configure debug and optimization options

CFLAGS         ?=
LDFLAGS        ?=
EXE_LDFLAGS    ?=
SHARED_LDFLAGS ?=
native         ?= 0
ifeq ($(UNAME), Windows)
    LIBFLAGS ?=
    ifeq ($(debug), 0)
        CFLAGS   += -O2 -DNDEBUG -Gs4096 -GL -MT
        LDFLAGS  += -LTCG
        LIBFLAGS += -LTCG
    else
        CFLAGS  += -D_DEBUG -Z7 -MTd
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

    gcov = 0

    LD_DEFS = -def:$1.win.def
else
    ifeq ($(debug), 0)
        CFLAGS += -O3 -DNDEBUG -ffunction-sections -fdata-sections
        CFLAGS += -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=1
        symbols ?= 0
        ifeq ($(symbols), 1)
            STRIP  ?= true
            CFLAGS += -ggdb -fno-omit-frame-pointer
        else
            STRIP ?= strip
            ifeq ($(UNAME), Linux)
                LDFLAGS += -Wl,--gc-sections -Wl,--as-needed
            endif
            ifeq ($(UNAME), Darwin)
                LDFLAGS += -Wl,-dead_strip
                ifeq ($(STRIP), strip)
                    STRIP += -x
                endif
            endif
        endif

        ifeq ($(native), 1)
            CFLAGS  += -march=native
            LDFLAGS += -march=native
        endif

        # Configure LTO, if available
        ifeq ($(UNAME), Linux)
            LTOAR ?= $(shell $(kos_dir)build/find_lto ar $(CC))
            ifeq (,$(LTOAR))
                lto ?= 0
            else
                ifeq (true,$(shell $(kos_dir)build/have_lto $(CC)))
                    lto ?= 1
                else
                    lto ?= 0
                endif
            endif
            ifneq ($(lto), 0)
                ifneq (,$(LTOAR))
                    AR = $(LTOAR)
                endif
                CFLAGS  += -flto -fno-fat-lto-objects
                LDFLAGS += -ffunction-sections -fdata-sections -flto=auto -fuse-linker-plugin
            endif
        endif
        ifeq ($(UNAME), Darwin)
            lto ?= 1
            ifneq ($(lto), 0)
                CFLAGS  += -flto
                LDFLAGS += -ffunction-sections -fdata-sections -flto
            endif
        endif
    else
        CFLAGS += -O0 -g
        STRIP  ?= true
    endif

    CFLAGS += -fPIC

    ifeq (true,$(shell $(kos_dir)build/have_visibility $(CC)))
        CFLAGS += -fvisibility=hidden -DKOS_SUPPORTS_VISIBILITY
    endif

    ifeq ($(UNAME), Darwin)
        SHARED_LDFLAGS += -dynamiclib -undefined dynamic_lookup
        LD_DEFS = -Wl,-exported_symbols_list -Wl,$1.macos.def
    else
        SHARED_LDFLAGS += -shared
        ifeq (true,$(shell $(kos_dir)build/have_dynamic_list $(CC)))
            LD_DEFS = -Wl,--dynamic-list=$1.gnu.def
        endif
    endif

    STRICTFLAGS = -Wextra -Werror
    STRICTFLAGS += -Wno-unused-parameter
    STRICTFLAGS += -Wshadow
    STRICTFLAGS += -Wunused
    #STRICTFLAGS += -Wnull-dereference
    STRICTFLAGS += -Wdouble-promotion
    STRICTFLAGS += -Wformat=2
    #STRICTFLAGS += -Wconversion
    STRICTFLAGS += -Wno-format-nonliteral # for vsnprintf

    ifdef tracy
        fastdispatch ?= 0
    endif
    fastdispatch ?= 1
    ifeq ($(fastdispatch), 1)
        CFLAGS += -DCONFIG_FAST_DISPATCH
    else
        STRICTFLAGS += -pedantic
    endif

    ifeq ($(UNAME), Linux)
        CFLAGS      += -D_BSD_SOURCE -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200112L
        EXE_LDFLAGS += -lpthread -lrt -ldl
    endif

    ifneq (true,$(shell $(kos_dir)build/have_o_cloexec $(CC) $(CFLAGS)))
        CFLAGS += -DCONFIG_NO_O_CLOEXEC
    endif

    ifneq (,$(filter FreeBSD OpenBSD NetBSD DragonFly,$(UNAME)))
        EXE_LDFLAGS += -lpthread
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
        ifeq ($(target), arm64)
            APPLE_FLAGS ?= -arch arm64 -mmacosx-version-min=11.0
        else
            APPLE_FLAGS ?= -mmacosx-version-min=10.10
        endif
    endif
    CFLAGS  += $(APPLE_FLAGS)
    LDFLAGS += $(APPLE_FLAGS)
endif

ifdef tracy
    deepstack ?= 1
endif
deepstack ?= 0
ifeq ($(deepstack), 1)
    CFLAGS += -DCONFIG_DEEP_STACK
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

ifdef tracy
    CFLAGS += -DTRACY_ENABLE -I $(tracy)
endif

ifdef version_major
    ifdef version_minor
        ifdef version_revision
            CFLAGS += -DKOS_VERSION_MAJOR=$(version_major)
            CFLAGS += -DKOS_VERSION_MINOR=$(version_minor)
            CFLAGS += -DKOS_VERSION_REVISION=$(version_revision)
        endif
    endif
endif

##############################################################################
# Optionally treat warnings as errors and add more checks

strict ?= 0 # Old compilers don't like it, disable by default

ifeq ($(strict), 1)
    CFLAGS += $(STRICTFLAGS)
endif

##############################################################################
# Filenames of object files and dependency files

ifeq ($(UNAME), Windows)
    o_suffix   = .obj
    a_suffix   = .lib
    exe_suffix = .exe
    so_suffix  = .dll
else
    o_suffix   = .o
    a_suffix   = .a
    exe_suffix =
    ifeq ($(UNAME), Darwin)
        so_suffix = .dylib
    else
        so_suffix = .so
    endif
endif

OBJECTS_FROM_SOURCES = $(addprefix $(out_dir)/, $(addsuffix $(o_suffix), $(basename $1)))
DEPS_FROM_SOURCES    = $(addprefix $(out_dir)/, $(addsuffix .d, $(basename $1)))

cpp_files ?=
c_files   ?=
d_files = $(call DEPS_FROM_SOURCES,$(cpp_files) $(c_files))

##############################################################################
# Declare 'default' as the first/default rule

default:

##############################################################################
# Include dependency files

-include $(d_files)

##############################################################################
# Clean rule

clean:
	rm -rf $(out_dir)

##############################################################################
# C and C++ rules

$(out_dir):
	@mkdir -p $@

ifeq ($(UNAME), Windows)

CL = $(out_dir_base)/../release/build/cldep/cldep$(exe_suffix)

$(out_dir)/$(notdir %.obj): %.c | $(out_dir)
ifeq (,$(filter c++%, $(CLANG_VER)))
	@echo C $(notdir $@)
else
	@echo C++ $(notdir $@)
endif
	@$(CL) -nologo -Wall $(CLANG)   $(CFLAGS) -Fo$@ -c $<

$(out_dir)/$(notdir %.obj): %.cpp | $(out_dir)
	@echo C++ $(notdir $@)
	@$(CL) -nologo -Wall $(CPPLANG) $(CFLAGS) -Fo$@ -c $<

else #------------------------------------------------------------------------

$(out_dir)/$(notdir %.o): %.c | $(out_dir)
ifeq (,$(filter c++%, $(CLANG_VER)))
	@echo C $(notdir $@)
else
	@echo C++ $(notdir $@)
endif
	@$(CC) -Wall $(CFLAGS) -c -MD $(CLANG)   $< -o $@
	@test -f $(<:.c=.d)   && mv $(<:.c=.d)   $(dir $@) || true # WAR for very old gcc

$(out_dir)/$(notdir %.o): %.cpp | $(out_dir)
	@echo C++ $(notdir $@)
	@$(CC) -Wall $(CFLAGS) -c -MD $(CPPLANG) $< -o $@
	@test -f $(<:.cpp=.d) && mv $(<:.cpp=.d) $(dir $@) || true # WAR for very old gcc

endif

##############################################################################
# Rule for linking executables

ifeq ($(UNAME), Windows)

define LINK_EXE
$(out_dir)/$1$(exe_suffix): $$(call OBJECTS_FROM_SOURCES,$2) $3
	@echo Link $(out_dir_rel)/$1$(exe_suffix)
	@link -nologo $(EXE_LDFLAGS) $(LDFLAGS) -out:$$@ $$^ $3
endef

else #------------------------------------------------------------------------

define LINK_EXE
$(out_dir)/$1$(exe_suffix): $$(call OBJECTS_FROM_SOURCES,$2) $3
	@echo Link $(out_dir_rel)/$1$(exe_suffix)
	@$(CXX) $$^ -o $$@ $3 $(EXE_LDFLAGS) $(LDFLAGS)
	@$(STRIP) $$@
endef

endif

##############################################################################
# Rule for linking shared libraries

SHARED_TARGET = $(out_dir)/$1$(so_suffix)

ifeq ($(UNAME), Windows)

define LINK_SHARED_LIB
$$(call SHARED_TARGET,$1): $$(call OBJECTS_FROM_SOURCES,$2)
	@echo Link $1$(so_suffix)
	@link -nologo -dll $(SHARED_LDFLAGS) $(LDFLAGS) -out:$$@ $$^ $3
endef

else #------------------------------------------------------------------------

define LINK_SHARED_LIB
$$(call SHARED_TARGET,$1): $$(call OBJECTS_FROM_SOURCES,$2)
	@echo Link $1$(so_suffix)
	@$(CXX) $$^ -o $$@ $3 $(SHARED_LDFLAGS) $(LDFLAGS)
	@$(STRIP) $$@
endef

endif

##############################################################################
# Rule for static libraries

ifeq ($(UNAME), Windows)
    LIB_NAME = $1$(a_suffix)
else
    LIB_NAME = lib$1$(a_suffix)
endif

LIB_TARGET = $(out_dir)/$(call LIB_NAME,$1)

ifeq ($(UNAME), Windows)

define LINK_STATIC_LIB
$$(call LIB_TARGET,$1): $$(call OBJECTS_FROM_SOURCES,$2)
	@echo Lib $$(notdir $$@)
	@rm -f $$@
	@lib -nologo $(LIBFLAGS) $$^ -out:$$@
endef

else

define LINK_STATIC_LIB
$$(call LIB_TARGET,$1): $$(call OBJECTS_FROM_SOURCES,$2)
	@echo Lib $$(notdir $$@)
	@rm -f $$@
	@$(AR) rcs $$@ $$^
endef

endif

##############################################################################
# Declare virtual targets

.PHONY: default clean
