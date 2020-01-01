#!/usr/bin/env python
""" Tests iteration over array using for-in loop. """

def run():
    """ Body of the test """

    loops = 300
    size = 10000
    elements = list(range(size))
    total = 0

    loop_idx = 0
    while loop_idx < loops:
        loop_idx += 1

        for elem in elements:
            total += elem

    print(total)

run()
