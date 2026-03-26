```SNOBOL4
*  SCRIP DEMO7 -- ROT13 cipher (SNOBOL4 section)
*  Idiom: REPLACE with two parallel 52-char alphabet strings
*  Ref: Gimpel UPLO.inc idiom
        &TRIM = 1
        PLAIN  = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz'
        ROT13  = 'NOPQRSTUVWXYZABCDEFGHIJKLMnopqrstuvwxyzabcdefghijklm'
        S      = 'Hello, World!'
        OUTPUT = REPLACE(S, PLAIN, ROT13)
        T      = REPLACE(REPLACE(S, PLAIN, ROT13), PLAIN, ROT13)
        OUTPUT = T
END
```

```Icon
# SCRIP DEMO7 -- ROT13 cipher (Icon section)
# Idiom: map() with two parallel alphabet strings
procedure main()
    plain := "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
    rot13 := "NOPQRSTUVWXYZABCDEFGHIJKLMnopqrstuvwxyzabcdefghijklm"
    s := "Hello, World!"
    write(map(s, plain, rot13))
    write(map(map(s, plain, rot13), plain, rot13))
end
```

```Prolog
% SCRIP DEMO7 -- ROT13 cipher (Prolog section)
% Idiom: maplist/2 on integer codes; rot13_code/2 arithmetic translation
:- initialization(main, main).

rot13_code(C, R) :-
    (   C >= 0'A, C =< 0'Z -> R is (C - 0'A + 13) mod 26 + 0'A
    ;   C >= 0'a, C =< 0'z -> R is (C - 0'a + 13) mod 26 + 0'a
    ;   R = C
    ).

rot13(S, Out) :-
    string_codes(S, Codes),
    maplist(rot13_code, Codes, OutCodes),
    string_codes(Out, OutCodes).

main :-
    rot13("Hello, World!", E), write(E), nl,
    rot13(E,               D), write(D), nl.
```
