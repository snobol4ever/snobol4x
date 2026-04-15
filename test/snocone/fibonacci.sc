// fibonacci.sc — recursive Fibonacci (SC-13)
procedure Fib(n) {
    if (LE(n, 1)) { Fib = n; return; }
    Fib = Fib(n - 1) + Fib(n - 2);
}
OUTPUT = Fib(0);
OUTPUT = Fib(1);
OUTPUT = Fib(2);
OUTPUT = Fib(5);
OUTPUT = Fib(10);
