% SCRIP DEMO2 -- Word Count (Prolog section)
% Idiom: DCG rules tokenise char list; phrase/3 counts words
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
