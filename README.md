The Kos Programming Language
============================

Kos is a general purpose scripting language with the following design goals:

* Easy to learn and easy to use.

* Useful for creating standalone scripts.

* Easy to embed in other applications.

* Dynamically typed, object-oriented, with elements of functional programming.

* Stable and reliable syntax, so your scripts will work in the future without
  any modifications.

* Small code base and small footprint.

* Simple, modern, feature-rich and powerful syntax, designed to reduce common
  types of bugs.

* Robust support for native multithreading.

* High quality code achieved with the help of the highest levels of warnings
  and every change tested in multiple ways, including full test suite running
  on Windows, Linux and MacOSX, Coverity and 100% test coverage.


Status
======

Kos is currently in development.  Most language features are already usable.

Refer to [Roadmap](doc/roadmap.md) for list of features planned.


Branch health (master)
======================

[![Build Status](https://travis-ci.org/kos-lang/kos.svg?branch=master)](https://travis-ci.org/kos-lang/kos)
[![Build Status](https://ci.appveyor.com/api/projects/status/github/kos-lang/kos?branch=master&svg=true)](https://ci.appveyor.com/project/cdragan/kos)
[![Coverage Status](https://coveralls.io/repos/github/kos-lang/kos/badge.svg?branch=master)](https://coveralls.io/github/kos-lang/kos?branch=master)
[![Coverity Scan](https://scan.coverity.com/projects/10189/badge.svg)](https://scan.coverity.com/projects/kos)

Documentation
=============

To find out more about Kos, follow these links:

* [Quick tutorial which will get you up to speed with Kos](doc/tutorial.md)
* [Instructions on how to compile Kos from source](doc/building.md)
* [Principles and ideas which influenced Kos design](doc/design.md)
* [List of possible ideas for future development](doc/proposals.md)
* [Formal description of Kos syntax](doc/grammar.md)
* [Information on how to contribute to Kos](doc/contributing.md)
* [Kos license](LICENSE.md)


Examples
========

This program prints "Hello, World!":

    #!/usr/bin/env kos
    import lang.print;

    print("Hello, World!\n");

This program prints 30 terms of the Fibonacci series:

    #!/usr/bin/env kos
    import lang.print;

    const terms = 30;
    var   a     = 0;
    var   b     = 1;

    print("Fib series: \(a)");

    for (var i = 0; i < terms; i += 1) {
        print(" \(b)");
        const c = a + b;
        a = b;
        b = c;
    }

    print("\n");

This program prints the first 1000 prime numbers:

    #!/usr/bin/env kos
    import lang.print;

    # Prime number generator with a fixed-size sieve
    fun primes(max_number)
    {
        yield 2; # Yield the only even prime number from the generator

        const sieve = []; # Empty array

        # Set array size; fills with 'void' values.
        # We set to half of the max number checked, because
        # we ignore even numbers and only check odd numbers.
        const len = max_number >> 1;
        sieve.resize(len);

        for (var value = 3; value < max_number; value += 2) {

            const idx = value >> 1;

            # Yield this number as prime if it hasn't been sieved-out
            if ( ! sieve[idx]) {

                yield value;

                # Mark all multiplicities of this prime as non-primes
                for (var i = idx + value; i < len; i += value) {
                    sieve[i] = true; # Mark a non-prime
                }
            }
        }
    }

    print("Prime numbers:");

    var count = 0;

    for (var value in primes(7920)) {
        print(" \(value)");
        count += 1;
    }

    print("\nPrinted \(count) primes\n");
