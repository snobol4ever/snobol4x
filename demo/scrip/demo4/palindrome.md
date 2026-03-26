```SNOBOL4
*  SCRIP DEMO4 -- Palindrome (SNOBOL4 section)
*  Idiom: IDENT(S, REVERSE(S)) -- one comparison, no loop
*  Ref: Gimpel/dotnet palin.sno idiom
        &TRIM = 1
        DEFINE('CHECK(S)')                  :(CHECK_END)
CHECK   S         = REPLACE(S, &LCASE, &UCASE)
        CHECK     = IDENT(S, REVERSE(S)) 'yes'  :S(RETURN)
        CHECK     = 'no'                    :(RETURN)
CHECK_END
        OUTPUT = CHECK('racecar')
        OUTPUT = CHECK('hello')
        OUTPUT = CHECK('level')
END
```

```Icon
# SCRIP DEMO4 -- Palindrome (Icon section)
# Idiom: subscript walk inward from both ends
procedure palindrome(s)
    s := map(s)
    i := 1
    j := *s
    while i < j do {
        if s[i] ~== s[j] then return "no"
        i +:= 1
        j -:= 1
    }
    return "yes"
end

procedure main()
    write(palindrome("racecar"))
    write(palindrome("hello"))
    write(palindrome("level"))
end
```

```Prolog
% SCRIP DEMO4 -- Palindrome (Prolog section)
% Idiom: reverse/2 built-in; unification does the comparison
:- initialization(main, main).

palindrome(S, yes) :- string_chars(S, Cs), reverse(Cs, Cs), !.
palindrome(_, no).

main :-
    palindrome("racecar", A), write(A), nl,
    palindrome("hello",   B), write(B), nl,
    palindrome("level",   C), write(C), nl.
```
