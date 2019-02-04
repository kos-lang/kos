#!/usr/bin/env python

from random import randint

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
