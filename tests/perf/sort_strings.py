#!/usr/bin/env python
""" Generates an array of random strings and then sorts it. """

from random import randint

def run():
    """ Body of the test """

    a = []

    i = 0
    while i < 100000:
        i += 1

        r = randint(0, 0x7FFFFFFFFFFFFFFF)

        s = ""

        while r:
            s += chr((r & 0x3F) + 0x20)
            r >>= 7

        a.append(s)

    a.sort()

run()
