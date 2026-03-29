main :-
    ( b @> a -> write(yes) ; write(no) ), nl,
    ( a @> b -> write(yes) ; write(no) ), nl,
    ( z @> z -> write(yes) ; write(no) ), nl.
main.
