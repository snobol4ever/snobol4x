:- assertz(fact(a)).
:- assertz(fact(b)).
:- assertz(fact(c)).

main :-
    abolish(fact/1),
    ( fact(_) -> write(found) ; write(gone) ), nl.
main.
