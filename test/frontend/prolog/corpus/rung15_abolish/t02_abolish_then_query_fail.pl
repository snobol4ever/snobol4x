:- assertz(dog(rex)).
:- assertz(dog(spot)).

main :-
    abolish(dog/1),
    ( dog(rex) -> write(yes) ; write(no) ), nl.
main.
