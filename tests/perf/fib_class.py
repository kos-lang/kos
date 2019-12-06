#!/usr/bin/env python
""" A test which generates Fibonacci's sequence """

class Fib:
    """ Fibonacci sequence generator """

    def __init__(self):
        """ Initializes the generator """
        self.prev = 1
        self.cur = 1

    def reset(self):
        """ Resets state of the generator """
        self.prev = 1
        self.cur = 1

    def get_prev(self):
        """ Returns the previous number in the sequence """
        return self.prev

    def get_cur(self):
        """ Returns the current number in the sequence """
        return self.cur

    def set_next(self, next_val):
        """ Sets the next number in the sequence """
        self.prev = self.cur
        self.cur = next_val

    def advance(self):
        """ Advances the generator """
        self.set_next(self.get_prev() + self.get_cur())

    def execute(self, num_loops):
        """ Runs one or more iterations of the generator """
        loop_idx = 0
        while loop_idx < num_loops:
            self.advance()
            loop_idx += 1
        ret = self.get_cur()
        self.reset()
        return ret

def run():
    """ Performs test loops """
    fib = Fib()
    orig = fib.execute(50)
    loop_idx = 0
    while loop_idx < 10000:
        if orig != fib.execute(50):
            raise Exception("Error")
        loop_idx += 1

    print(orig)

run()
