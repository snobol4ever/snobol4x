main :-
    ( retract(ghost(x)) -> write(found) ; write(notfound) ), nl.
