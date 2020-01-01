function new_fib()
{
    this.prev = 1;
    this.cur  = 1;
}

new_fib.prototype.get_prev = function() { return this.prev; };
new_fib.prototype.get_cur  = function() { return this.cur;  };

new_fib.prototype.reset = function() {
    this.prev = 1;
    this.cur  = 1;
};

new_fib.prototype.set_next = function(next) {
    this.prev = this.cur;
    this.cur  = next;
};

new_fib.prototype.advance = function() {
    this.set_next( this.get_prev() + this.get_cur() );
};

new_fib.prototype.execute = function(n) {
    for (let i = 0; i < n; i++) {
        this.advance();
    }
    const ret = this.get_cur();
    this.reset();
    return ret;
};

const fib = new new_fib();

const orig = fib.execute(50);

for (let i = 0; i < 10000; i++) {
    if (orig != fib.execute(50)) {
        print("FAIL loop " + i);
        break;
    }
}

print(orig);
