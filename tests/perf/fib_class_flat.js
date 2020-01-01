const fib = {
    prev: 1,
    cur:  1,

    get_prev: function() { return this.prev; },
    get_cur:  function() { return this.cur; },

    reset: function() {
        this.prev = 1
        this.cur  = 1
    },

    set_next: function(next) {
        this.prev = this.cur;
        this.cur  = next;
    },

    advance: function() {
        this.set_next( this.get_prev() + this.get_cur() );
    },

    execute: function(n) {
        for (let i = 0; i < n; i++) {
            this.advance();
        }
        const ret = this.get_cur();
        this.reset();
        return ret;
    }
};

const orig = fib.execute(50);

for (let i = 0; i < 10000; i++) {
    if (orig != fib.execute(50)) {
        console.log("FAIL loop " + i);
        break;
    }
}

console.log(orig)
