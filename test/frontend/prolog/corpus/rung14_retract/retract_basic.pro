:- assertz(color(red)).
:- assertz(color(green)).
:- assertz(color(blue)).

main :-
    retract(color(green)),
    color(X),
    write(X), nl,
    fail.
main.
