:- assertz(person(alice, 30)).
:- assertz(person(bob, 25)).
:- assertz(person(carol, 35)).

main :-
    person(Name, Age),
    write(Name), write(' '), write(Age), nl,
    fail.
main.
