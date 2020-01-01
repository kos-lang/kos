const loops = 1000;
const size  = 10000;
const a     = [];
let   total = 0;

for (let l = 0; l < loops; l++) {
    for (let i = 0; i < size; i++) {
        a[i] = i;
    }

    for (let i = 0; i < size; i++) {
        total += a[i];
    }
}

console.log(total);
