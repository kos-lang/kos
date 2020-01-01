#!/usr/bin/env python
""" Tests iteration over array using explicit index. """

def run():
    """ Body of the test """

    loops = 1000
    size = 10000
    elements = list(range(size))
    total = 0

    loop_idx = 0
    while loop_idx < loops:
        loop_idx += 1

        i = 0
        while i < size:
            elements[i] = i
            i += 1

        i = 0
        while i < size:
            total += elements[i]
            i += 1

    print(total)

run()
