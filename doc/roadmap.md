Roadmap
=======

The following features are planned before first release:

* Update C++ API to be compatible with GC
* Language features:
    - set/get
    - static
    - Named arguments
* Optimizations:
    - Inlining
    - Compute local variable use range, release registers early
    - Reduce cost of function calls
    - Hoist constants outside of loops
    - Use linear search for small objects
    - Use GCC's computed goto extension for instruction dispatch
* Modules:
    - regex
    - debug
    - fs improvements (e.g. directory scanning)
    - datetime improvements (e.g. timestamp formatting)
    - net
    - run (for launching other programs)
* Debugger:
    - Build symbol table for debugger
    - Export debugger API (debug module)
    - The debugger written in Kos
* Integers of unlimited size (bigint)
* Missing functionality (TODOs)
* Usability fixes (e.g. better error messages)
