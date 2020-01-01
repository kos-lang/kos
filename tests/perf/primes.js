// Prime number generator with a fixed-size sieve
function primes(max_number, generate)
{
    generate(2); // Yield the only even prime number from the generator

    // Fill buffer with '0' values.
    // We set size to half of the max number checked, because
    // we ignore even numbers and only check odd numbers.
    const len   = max_number >> 1;
    const sieve = [];
    sieve.length = len;

    for (let value = 3; value <= max_number; value += 2) {

        const idx = value >> 1;

        // Yield this number as prime if it hasn't been sieved-out
        if ( ! sieve[idx]) {

            generate(value);

            // Mark all multiplicities of this prime as non-primes
            for (let i = idx + value; i < len; i += value) {
                sieve[i] = 1; // Mark a non-prime
            }
        }
    }
}

let count = 0;
let last  = null;

primes(15485863, function(value) {
    last  = value
    count += 1
});

print(count + "th prime is " + last)
