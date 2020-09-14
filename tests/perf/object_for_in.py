#!/usr/bin/env python
""" Tests iteration over dict using for-in loop. """

def run():
    """ Body of the test """

    lcg_init = 48611 # init value for LCG RNG
    lcg_mult = 16807 # multiplier for LCG RNG

    rng = lcg_init
    obj = { }
    num_objs = 1000
    loop_idx = 0
    while loop_idx < num_objs:
        rng = (rng * lcg_mult) & 0xFFFF
        obj[str(rng)] = loop_idx
        loop_idx += 1

    loops = 300
    total = 0

    loop_idx = 0
    while loop_idx < loops:
        loop_idx += 1

        for key in obj:
            total += obj[key]

    print(total)


run()
