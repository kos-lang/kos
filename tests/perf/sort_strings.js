function rand_integer(min, max)
{
    return Math.floor(Math.random() * (max + 1 - min)) + min;
}

const a = [];

const str_codes = [];

for (let i = 0; i < 100000; i++) {
    
    str_codes.length = 0;

    const r64 = rand_integer(0, 0x7FFFFFFFFFFFFFFF);
    const r32 = [ (r64 / 0x100000000) & 0xFFFFFFFF, r64 & 0xFFFFFFFF ];

    for (let x = 0; x < 2; x++) {
        let r = r32[x];

        while (r) {
            str_codes.push((r & 0x3F) + 0x20);
            r >>= 7;
        }
    }

    a.push(String.fromCharCode.apply(null, str_codes));
}

a.sort();
