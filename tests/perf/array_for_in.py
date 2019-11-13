#!/usr/bin/env python
""" Tests iteration over array using for-in loop. """

def run():
    """ Body of the test """

    loops = 3000
    size = 10000
    a = list(range(size))
    total = 0

    l = 0
    while l < loops:
        l += 1

        for elem in a:
            total += elem

    print(total)

run()
