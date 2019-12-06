#!/usr/bin/env python
""" Generates an array of random strings and then sorts it. """

from random import randint

def run():
    """ Body of the test """

    ary = []

    loop_idx = 0
    while loop_idx < 100000:
        loop_idx += 1

        rnd_num = randint(0, 0x7FFFFFFFFFFFFFFF)

        new_string = ""

        while rnd_num:
            new_string += chr((rnd_num & 0x3F) + 0x20)
            rnd_num >>= 7

        ary.append(new_string)

    ary.sort()

run()
