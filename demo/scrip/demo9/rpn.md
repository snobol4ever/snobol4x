```SNOBOL4
*  SCRIP DEMO9 -- RPN calculator (SNOBOL4 section)
*  Idiom: POS(0)-anchored patterns; Gimpel PUSH/POP linked-list stack
*  Expression: '5 1 2 + 4 * + 3 -'  =>  5 + ((1+2)*4) - 3 = 14
        &CASE  = 1
        &TRIM  = 1
        DATA('link(lnk_next,lnk_val)')
        DEFINE('push(x)')
        DEFINE('pop()')                     :(push_end)
push    stk    = link(stk, x)
        push   = .lnk_val(stk)             :(NRETURN)
pop     IDENT(stk)                          :S(FRETURN)
        pop    = lnk_val(stk)
        stk    = lnk_next(stk)             :(RETURN)
push_end
        digits = POS(0) SPAN('0123456789')
        ops    = POS(0) ANY('+-*/')
        blanks = POS(0) SPAN(' ')
        expr   = '5 1 2 + 4 * + 3 -'
tok_loop
        expr   blanks =
get_tok expr   digits . tok =              :S(do_num)
        expr   ops . tok =                  :S(do_op)
                                            :(done)
do_num  push(tok)                           :(tok_loop)
do_op   a      = pop()
        b      = pop()
        IDENT(tok, '+')                     :F(try_minus)
        push(b + a)                         :(tok_loop)
try_minus
        IDENT(tok, '-')                     :F(try_star)
        push(b - a)                         :(tok_loop)
try_star
        IDENT(tok, '*')                     :F(try_slash)
        push(b * a)                         :(tok_loop)
try_slash
        push(b / a)
done    OUTPUT = pop()
END
```

```Icon
# SCRIP DEMO9 -- RPN calculator (Icon section)
# Idiom: list as stack; push/pop via put()/pull(); scanning with tab()
procedure main()
    expr := "5 1 2 + 4 * + 3 -"
    tokens := []
    expr ? while not pos(0) do {
        tab(many(' ') | &pos)
        tok := tab(many(&digits)) | move(1)
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
% Idiom: rpn/3 with explicit stack argument; eval/4 operator dispatch
:- initialization(main, main).

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
