```SNOBOL4
*  SCRIP DEMO5 -- Fibonacci first 10 (SNOBOL4 section)
*  Idiom: iterative labeled-goto loop; two accumulators
        &TRIM = 1
        A     = 0
        B     = 1
        N     = 0
LOOP    OUTPUT = A
        N      = N + 1
        LT(N, 10)                           :F(END)
        T      = B
        B      = A + B
        A      = T                          :(LOOP)
END
```

```Icon
# SCRIP DEMO5 -- Fibonacci first 10 (Icon section)
# Idiom: suspend generator produces the sequence lazily
procedure fibs()
    a := 0
    b := 1
    repeat {
        suspend a
        a :=: b
        b +:= a
    }
end

procedure main()
    every write(fibs() \ 10)
end
```

```Prolog
% SCRIP DEMO5 -- Fibonacci first 10 (Prolog section)
% Idiom: fib/2 accumulator rule; forall drives output
:- initialization(main, main).

fib(N, F) :- fib(N, 0, 1, F).
fib(0, F, _, F) :- !.
fib(N, A, B, F) :- N > 0, N1 is N - 1, B1 is A + B, fib(N1, B, B1, F).

main :-
    forall(between(0, 9, N), (fib(N, F), write(F), nl)).
```
