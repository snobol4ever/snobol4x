:- assertz(cat(whiskers)).
:- assertz(cat(mittens)).
:- assertz(bird(tweety)).
:- assertz(bird(polly)).

main :-
    abolish(cat/1),
    ( cat(_) -> write(cat_found) ; write(cat_gone) ), nl,
    bird(X), write(X), nl, fail.
main.
