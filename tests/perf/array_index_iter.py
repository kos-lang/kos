#!/usr/bin/env python
""" Tests iteration over array using explicit index. """

def run():
    """ Body of the test """

    loops = 10000
    size  = 10000
    a     = list(range(size))
    total = 0

    l = 0
    while l < loops:
        l += 1

        i = 0
        while i < size:
            a[i] = i
            i += 1

        i = 0
        while i < size:
            total += a[i]
            i += 1

    print(total)

run()
