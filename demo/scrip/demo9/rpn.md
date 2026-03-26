```SNOBOL4
*  SCRIP DEMO9 -- RPN calculator (SNOBOL4 section)
*  Idiom: pattern-driven token scan; Gimpel PUSH/POP linked-list stack
*  Input: "3 4 + 2 * 7 1 - /" => ((3+4)*2) / (7-1) = 14/6 ... use integers: "3 4 2 * 1 5 - 2 3 ^ ^ / +" no
*  Use: "5 1 2 + 4 * + 3 -"  => 5 + ((1+2)*4) - 3 = 14
        &TRIM = 1
        DATA('LINK(NEXT,VAL)')
        DEFINE('PUSH(X)')
        DEFINE('POP()')                     :(PUSH_END)
PUSH    STK   = LINK(STK, X)
        PUSH  = .VAL(STK)                   :(NRETURN)
POP     IDENT(STK)                          :S(FRETURN)
        POP   = VAL(STK)
        STK   = NEXT(STK)                   :(RETURN)
PUSH_END
        DIGITS = SPAN('0123456789')
        EXPR   = '5 1 2 + 4 * + 3 -'
LOOP    EXPR   SPAN(' ') =                  :F(TOKEN)
TOKEN   EXPR   DIGITS . TOK =               :S(NUM)
        EXPR   LEN(1) . TOK =               :F(DONE)
        A      = POP()
        B      = POP()
        TOK    = IDENT(TOK,'+') B + A       :S(PUSH)
        TOK    = IDENT(TOK,'-') B - A       :S(PUSH)
        TOK    = IDENT(TOK,'*') B * A       :S(PUSH)
        TOK    = IDENT(TOK,'/') B / A       :S(PUSH)
NUM     PUSH(TOK)                           :(LOOP)
PUSH    PUSH(TOK)                           :(LOOP)
DONE    OUTPUT = POP()
END
```

```Icon
# SCRIP DEMO9 -- RPN calculator (Icon section)
# Idiom: list as stack; push/pop via put()/pull(); every token via !tokens
procedure main()
    expr := "5 1 2 + 4 * + 3 -"
    tokens := []
    expr ? while not pos(0) do {
        tab(many(' ') | &pos)
        tok := (tab(many(&digits)) | move(1)) | break
        put(tokens, tok)
    }
    stack := []
    every tok := !tokens do {
        if tok == ("+" | "-" | "*" | "/") then {
            b := pull(stack)
            a := pull(stack)
            put(stack, case tok of {
                "+": a + b
                "-": a - b
                "*": a * b
                "/": a / b
            })
        } else
            put(stack, integer(tok))
    }
    write(pull(stack))
end
```

```Prolog
% SCRIP DEMO9 -- RPN calculator (Prolog section)
% Idiom: DCG tokenises input; rpn/3 evaluates with explicit stack argument
:- initialization(main, main).

tokens([], []).
tokens([T|Ts], [H|Hs]) :-
    (number_codes(T, H) -> true ; atom_codes(T, H)),
    tokens(Ts, Hs).

rpn([], [Result], Result).
rpn([T|Ts], Stack, Result) :-
    number(T), !,
    rpn(Ts, [T|Stack], Result).
rpn([Op|Ts], [B,A|Stack], Result) :-
    eval(Op, A, B, V),
    rpn(Ts, [V|Stack], Result).

eval(+, A, B, V) :- V is A + B.
eval(-, A, B, V) :- V is A - B.
eval(*, A, B, V) :- V is A * B.
eval(/, A, B, V) :- V is A / B.

main :-
    rpn([5, 1, 2, +, 4, *, +, 3, -], [], Result),
    write(Result), nl.
```
