#!/usr/bin/env python
""" Test which generates prime numbers """

def primes(max_number):
    """ Prime number generator with a fixed-size sieve """

    yield 2 # Yield the only even prime number from the generator

    # Fill array with False values.
    # We set size to half of the max number checked, because
    # we ignore even numbers and only check odd numbers.
    size = max_number >> 1
    sieve = [False] * size

    value = 3
    while value < max_number:

        idx = value >> 1

        # Yield this number as prime if it hasn't been sieved-out
        if not sieve[idx]:

            yield value

            # Mark all multiplicities of this prime as non-primes
            i = idx + value
            while i < size:
                sieve[i] = True # Mark a non-prime
                i += value

        value += 2

def run():
    """ Runs the test """

    count = 0
    last = None

    for value in primes(15485864):
        last = value
        count += 1

    print(str(count)+"th prime is "+str(last))

run()
