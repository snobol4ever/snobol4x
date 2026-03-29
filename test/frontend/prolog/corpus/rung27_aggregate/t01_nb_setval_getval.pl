:- initialization(main).
main :-
    nb_setval(counter, 0),
    nb_setval(counter, 42),
    nb_getval(counter, V),
    write(V), nl.
