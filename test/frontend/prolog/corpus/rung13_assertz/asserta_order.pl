:- assertz(item(b)).
:- assertz(item(c)).
:- asserta(item(a)).

main :-
    item(X),
    write(X), nl,
    fail.
main.
