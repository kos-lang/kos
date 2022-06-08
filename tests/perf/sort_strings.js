function rand_integer(min, max)
{
    return Math.floor(Math.random() * (max + 1 - min)) + min;
}

const a = [];

const str_codes = [];

for (let i = 0; i < 100000; i++) {
    
    str_codes.length = 0;

    const r64 = rand_integer(0, 0x7FFFFFFFFFFFFFFF);
    var   r1  = (r64 / 0x100000000) & 0xFFFFFFFF;
    var   r0  = r64 & 0xFFFFFFFF;

    for (let j = 0; j < 4; j++) {
        str_codes[j] = (r1 & 0x3F) + 0x20;
        r1 = r1 >> 7;
    }

    for (let j = 4; j < 8; j++) {
        str_codes[j] = (r0 & 0x3F) + 0x20;
        r0 = r0 >> 7;
    }

    str_codes[8] = (((r0 << 4) + r1) & 0x3F) + 0x20;

    a.push(String.fromCharCode.apply(null, str_codes));
}

a.sort();
