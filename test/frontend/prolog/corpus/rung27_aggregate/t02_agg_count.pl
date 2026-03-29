:- initialization(main).
fruit(apple). fruit(banana). fruit(cherry).
main :-
    aggregate_all(count, fruit(_), N),
    write(N), nl.
