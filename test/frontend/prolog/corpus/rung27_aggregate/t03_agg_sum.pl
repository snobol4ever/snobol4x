:- initialization(main).
score(10). score(20). score(30).
main :-
    aggregate_all(sum(S), score(S), Total),
    write(Total), nl.
