:- assertz(animal(cat)).
:- assertz(animal(dog)).
:- assertz(animal(bird)).
:- assertz(animal(fish)).

main :-
    animal(X),
    write(X), nl,
    fail.
main.
