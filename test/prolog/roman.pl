% SCRIP DEMO3 -- Roman Numerals (Prolog section)
% Idiom: arithmetic rules map value to numeral via recursive subtraction
:- initialization(main, main).

roman(0, '') :- !.
roman(N, R) :- N >= 1000, !, N1 is N - 1000, roman(N1, R1), atom_concat('M',  R1, R).
roman(N, R) :- N >= 900,  !, N1 is N - 900,  roman(N1, R1), atom_concat('CM', R1, R).
roman(N, R) :- N >= 500,  !, N1 is N - 500,  roman(N1, R1), atom_concat('D',  R1, R).
roman(N, R) :- N >= 400,  !, N1 is N - 400,  roman(N1, R1), atom_concat('CD', R1, R).
roman(N, R) :- N >= 100,  !, N1 is N - 100,  roman(N1, R1), atom_concat('C',  R1, R).
roman(N, R) :- N >= 90,   !, N1 is N - 90,   roman(N1, R1), atom_concat('XC', R1, R).
roman(N, R) :- N >= 50,   !, N1 is N - 50,   roman(N1, R1), atom_concat('L',  R1, R).
roman(N, R) :- N >= 40,   !, N1 is N - 40,   roman(N1, R1), atom_concat('XL', R1, R).
roman(N, R) :- N >= 10,   !, N1 is N - 10,   roman(N1, R1), atom_concat('X',  R1, R).
roman(N, R) :- N >= 9,    !, N1 is N - 9,    roman(N1, R1), atom_concat('IX', R1, R).
roman(N, R) :- N >= 5,    !, N1 is N - 5,    roman(N1, R1), atom_concat('V',  R1, R).
roman(N, R) :- N >= 4,    !, N1 is N - 4,    roman(N1, R1), atom_concat('IV', R1, R).
roman(N, R) :- N >= 1,    !, N1 is N - 1,    roman(N1, R1), atom_concat('I',  R1, R).

main :-
    roman(1776, A), write(A), nl,
    roman(42,   B), write(B), nl,
    roman(9,    C), write(C), nl.
