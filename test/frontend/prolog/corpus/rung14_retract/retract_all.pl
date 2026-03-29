:- assertz(item(a)).
:- assertz(item(b)).
:- assertz(item(c)).

retract_loop :-
    retract(item(_)),
    retract_loop.
retract_loop.

main :-
    retract_loop,
    ( item(_) -> write(notempty) ; write(empty) ), nl.
