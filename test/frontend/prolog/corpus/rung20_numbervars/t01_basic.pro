:- initialization(main).
main :- numbervars(f(X,Y,X), 0, End), write(f(X,Y,X)), nl, write(End), nl.
