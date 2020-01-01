const loops = 300;
const size  = 10000;
const a     = [];
let   total = 0;

a.length = size;
for (let i = 0; i < size; i++) {
    a[i] = i;
}

for (let l = 0; l < loops; l += 1) {

    for (let elem in a) {
        total += a[elem];
    }
}

print(total);
