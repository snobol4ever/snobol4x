:- initialization(main).
num(1). num(2). num(3).
main :-
    findall(Y, (num(X), Y is X * X), Ys),
    write(Ys), nl.
