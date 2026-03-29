:- initialization(main).
color(red). color(green). color(blue).
main :-
    findall(X, color(X), Xs),
    write(Xs), nl.
