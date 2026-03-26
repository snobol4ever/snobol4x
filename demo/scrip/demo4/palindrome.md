```SNOBOL4
*  SCRIP DEMO4 -- Palindrome (SNOBOL4 section)
*  Idiom: IDENT(s, REVERSE(s)) -- one comparison, no loop
*  Ref: Gimpel/dotnet palin.sno idiom
        &CASE  = 1
        &TRIM  = 1
        DEFINE('check(s)')                  :(check_end)
check   s      = REPLACE(s, &LCASE, &UCASE)
                 IDENT(s, REVERSE(s))       :F(check_no)
        check  = 'yes'                      :(RETURN)
check_no
        check  = 'no'                       :(RETURN)
check_end
        OUTPUT = check('racecar')
        OUTPUT = check('hello')
        OUTPUT = check('level')
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
