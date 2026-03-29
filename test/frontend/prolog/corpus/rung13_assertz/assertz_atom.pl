:- assertz(color(red)).
:- assertz(color(green)).
:- assertz(color(blue)).

main :-
    color(X),
    write(X), nl,
    fail.
main.
