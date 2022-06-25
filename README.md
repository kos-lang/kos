![img](interpreter/artwork/kos_64.png)
The Kos Programming Language
============================

Kos is a general purpose scripting language, which builds on top of the most
successful scripting languages, but is designed with usability in mind.

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

Status
------

Kos is currently in development.  Most language features are already usable.

Refer to [Roadmap](doc/roadmap.md) for list of features planned.

Branch health (master)
----------------------

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Build Status](https://github.com/kos-lang/kos/workflows/Tests/badge.svg)](https://github.com/kos-lang/kos/actions)
[![Coverage Status](https://coveralls.io/repos/github/kos-lang/kos/badge.svg?branch=master)](https://coveralls.io/github/kos-lang/kos?branch=master)
[![Coverity Scan](https://scan.coverity.com/projects/10189/badge.svg)](https://scan.coverity.com/projects/kos)
[![Lgtm alerts](https://img.shields.io/lgtm/alerts/g/kos-lang/kos.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/kos-lang/kos/alerts/)

Documentation
-------------

To find out more about Kos, follow these links:

* [Quick Start](doc/quickstart.md)
* [Tutorial](doc/tutorial.md)
* [Library Reference](doc/modules.md)
* [How to compile Kos from source](doc/building.md)
* [Principles and ideas which influenced Kos design](doc/design.md)
* [List of possible ideas for future development](doc/proposals.md)
* [Formal description of Kos syntax](doc/grammar.md)
* [Information on how to contribute to Kos](doc/contributing.md)
* [Kos license](LICENSE)

Examples
--------

This program prints "Hello, World!":

```javascript
#!/usr/bin/env kos
import base.print

print("Hello, World!")
```

This program prints 30 terms of the Fibonacci series:

```javascript
#!/usr/bin/env kos
import base: print, range

const terms = 30
var   a     = 0
var   b     = 1

print("Fib series:")
print("    \(a)")

for var i in range(terms - 1) {
    print("    \(b)")
    const next = a + b
    a = b
    b = next
}
```

This program prints the first 1000 prime numbers:

```javascript
#!/usr/bin/env kos
import base: print, range

# Prime number generator with a fixed-size sieve
fun primes(max_number)
{
    yield 2 # Yield the only even prime number from the generator

    const sieve = [] # Empty array

    # Set array size, fills with 'void' values.
    # We set to half of the max number checked, because
    # we ignore even numbers and only check odd numbers.
    const len = max_number >> 1
    sieve.resize(len)

    # Find and yield all odd primes
    for var value in range(3, max_number, 2) {

        const idx = value >> 1

        # Yield this number as prime if it hasn't been sifted-out
        if ! sieve[idx] {

            yield value

            # Mark all multiplicities of this prime as non-primes
            for var i in range(idx + value, len, value) {
                sieve[i] = true # Mark a non-prime
            }
        }
    }
}

public fun main
{
    print("Prime numbers:")

    var count = 0

    for var value in primes(7920) {
        print("    \(value)")
        count += 1
    }

    print("Printed \(count) primes")
}
```
