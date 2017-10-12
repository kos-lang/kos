#!/usr/bin/env python

class Fib:

    def __init__(self):
        self.prev = 1
        self.cur  = 1

    def Reset(self):
        self.prev = 1
        self.cur  = 1

    def GetPrev(self):
        return self.prev

    def GetCur(self):
        return self.cur

    def SetNext(self, next):
        self.prev = self.cur
        self.cur  = next

    def Advance(self):
        self.SetNext(self.GetPrev() + self.GetCur())

    def Execute(self, n):
        i = 0
        while i < n:
            self.Advance()
            i += 1
        ret = self.GetCur()
        self.Reset()
        return ret

fib = Fib()
orig = fib.Execute(50)
i = 0
while i < 10000:
    if orig != fib.Execute(50):
        raise Exception("Error")
    i += 1

print(orig)
