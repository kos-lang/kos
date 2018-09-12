#!/usr/bin/env python

loops = 20000
size  = 10000
a     = list(range(size))
total = 0

l = 0
while l < loops:
    l += 1

    for elem in a:
        total += elem

print(total)
