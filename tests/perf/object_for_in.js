const lcg_init = 48611 // init value for LCG RNG
const lcg_mult = 16807 // multiplier for LCG RNG

let rng = lcg_init
let obj = { }
const num_objs = 1000
for (let i = 0; i < num_objs; i++) {
    rng = (rng * lcg_mult) & 0xFFFF
    obj["" + rng] = i
}

const loops = 300
let   total = 0

for (let l = 0; l < loops; l++) {

    for (let key in obj) {
        total += obj[key]
    }
}

console.log(total)
