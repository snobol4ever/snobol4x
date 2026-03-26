:- assertz(color(red)).
:- assertz(color(blue)).

main :-
    abolish(color/1),
    assertz(color(green)),
    assertz(color(yellow)),
    color(X), write(X), nl, fail.
main.
