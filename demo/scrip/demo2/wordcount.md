```SNOBOL4
*  SCRIP DEMO2 -- Word Count (SNOBOL4 section)
*  Idiom: SPAN consumes a word; loop accumulates count
*  Input:  fixed string
*  Output: integer word count
        STR   = 'the quick brown fox jumps over the lazy dog'
        WORDS = SPAN(&LCASE &UCASE)
        COUNT = 0
LOOP    STR   WORDS . W          :F(DONE)
        COUNT = COUNT + 1
        STR   LEN(SIZE(W)) = ''  :S(LOOP)
DONE    OUTPUT = COUNT
END
```

```Icon
# SCRIP DEMO2 -- Word Count (Icon section)
# Idiom: !str character generator with move()/tab() scanning
procedure main()
    s := "the quick brown fox jumps over the lazy dog"
    count := 0
    s ? {
        while tab(upto(&letters)) do {
            tab(many(&letters))
            count +:= 1
        }
    }
    write(count)
end
```

```Prolog
% SCRIP DEMO2 -- Word Count (Prolog section)
% Idiom: DCG rule tokenises char list into word list
:- initialization(main, main).

whites --> [].
whites --> [C], { char_type(C, space) }, whites.

word([C|Cs]) --> [C], { char_type(C, alpha) }, word(Cs).
word([])     --> [].

words([])     --> whites.
words([W|Ws]) --> whites, word(W), { W \= [] }, words(Ws).

count_words(Str, N) :-
    string_chars(Str, Chars),
    phrase(words(Ws), Chars, []),
    length(Ws, N).

main :-
    count_words("the quick brown fox jumps over the lazy dog", N),
    write(N), nl.
```
