Roadmap
=======

The following features are planned before first release:

* Language features:
    - set/get
    - static
* Optimizations:
    - Inlining
    - Compute local variable use range, release registers early
    - Reduce cost of function calls
    - Hoist constants outside of loops
    - Use linear search for small objects
* Modules:
    - debug
    - datetime improvements (e.g. timestamp formatting)
    - ssl - secure socket implemented using OS-specific API
* Debugger:
    - Build symbol table for debugger
    - Export debugger API (debug module)
    - The debugger written in Kos
* Integers of unlimited size (bigint)
* Missing functionality (TODOs)
* Usability fixes (e.g. better error messages)
