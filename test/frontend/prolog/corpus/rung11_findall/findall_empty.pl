:- initialization(main).
main :-
    findall(X, fail, Xs),
    write(Xs), nl.
