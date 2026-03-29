:- initialization(main).
num(1). num(2). num(3). num(4). num(5).
even(X) :- num(X), 0 is X mod 2.
main :-
    findall(X, even(X), Xs),
    write(Xs), nl.
