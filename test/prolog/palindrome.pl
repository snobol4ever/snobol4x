% SCRIP DEMO4 -- Palindrome (Prolog section)
% Idiom: reverse/2 built-in; unification does the comparison
:- initialization(main, main).

palindrome(S, yes) :- string_chars(S, Cs), reverse(Cs, Cs), !.
palindrome(_, no).

main :-
    palindrome("racecar", A), write(A), nl,
    palindrome("hello",   B), write(B), nl,
    palindrome("level",   C), write(C), nl.
