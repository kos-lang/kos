Compiling Kos from source
=========================

This document describes how to compile Kos from source on various platforms.

If you are compiling Kos for Windows, please check the Windows section below
before continuing.

To compile Kos, simply run:

    make

To speed up the build, you can also add `-j 2` (replace 2 with the number of
available CPUs or CPU cores).  For example:

    make -j 2

It is also recommended to test whether the build works correctly, by running
the Kos test suite:

    make test

Note: Kos Makefiles use GNU make.  On some Operating Systems, like FreeBSD, you
need to invoke `gmake` instead of `make`.


Installation
============

To install Kos compiled from source in its target location, run:

    make install

This installs Kos in /usr on non-Windows systems.  It may be necessary
to run `make` with `sudo`:

    sudo make install

To specify a different installation directory than the default, especially
when only installing for the current user, use the `destdir` variable,
like so:

    make install destdir=../mydir

Both absolute and relative directories are supported.  If directory names
contain spaces, double quotes must be used and the spaces must be escaped
with a backslash:

    make install "destdir=../my\ dir"

The Kos executable is installed in `$destdir/bin` and Kos modules are installed
in `$destdir/share/kos/modules`.

Finally, if Kos is installed in non-default directory, the PATH environment
variable must be updated in shell startup scripts to point to the Kos
executable directory.


Windows
=======

Kos for Windows is compiled with Visual C++ compiler, but the compilation is
driven by GNU Make, which can be obtained with MSYS2 or Cygwin.

Before you can compile Kos on Windows, you need to install:

* Visual Studio, in particular the Visual C++ compiler (cl.exe)
* MSYS2 or Cygwin

Find the startup script which you use to start MSYS2 or Cygwin (e.g. msys.bat
or cygwin.bat), edit it and add the following line(s) near the beginning of
the script:

* For 64-bit toolchain, add:

        call "%VC140COMNTOOLS%VCVarsQueryRegistry.bat" 64bit
        call "%VCINSTALLDIR%vcvarsall.bat" amd64

* For 32-bit toolchain, add:

        call "%VC140COMNTOOLS%vcvars32.bat"

Note: Examine your environment to find the right variable pointing to
Common Tools directory, VC140COMNTOOLS is right for Visual Studio 2015.

When installing Kos on Windows, `destdir` defaults to `C:\Program Files`
directory, Kos executable is installed in `C:\Program Files\Kos` and
modules are installed in `C:\Program Files\Kos\modules`.

The registry can be updated on Windows to associate the `kos` extension
with the Kos executable.


Cross-compiling
===============

If you're cross-compiling Kos, set up the following environment variables to
point to your cross-toolchain:

* **CC** - path to the C/C++ compiler
* **CXX** - path to the C++ compiler used for linking
* **AR** - path to ar
* **STRIP** - path to strip for release builds


Optional configuration
======================

These optional settings can be either passed to make on the command line or
set as environment variables:

* **debug=1** - Enables debug build (default is release).
* **strict=1** - Enables strict warnings, treats warnings as errors.
* **native=1** - (Non-Windows platforms) Enables optimizations for the
current system; the produced executables may not work on other, older systems.
* **destdir=path** - Specifies installation directory, relative or absolute.
