#!/usr/bin/env python

# Prime number generator with a fixed-size sieve
def primes(max_number):

    yield 2 # Yield the only even prime number from the generator

    # Fill array with False values.
    # We set size to half of the max number checked, because
    # we ignore even numbers and only check odd numbers.
    len   = max_number >> 1
    sieve = [ False ] * len

    value = 3
    while value < max_number:

        idx = value >> 1

        # Yield this number as prime if it hasn't been sieved-out
        if not sieve[idx]:

            yield value

            # Mark all multiplicities of this prime as non-primes
            i = idx + value
            while i < len:
                sieve[i] = True # Mark a non-prime
                i += value

        value += 2

count = 0
last  = None

for value in primes(15485864):
    last  = value
    count += 1

print(str(count)+"th prime is "+str(last))
