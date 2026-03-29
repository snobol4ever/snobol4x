main :-
    ( a @< b -> write(yes) ; write(no) ), nl,
    ( b @< a -> write(yes) ; write(no) ), nl,
    ( a @< a -> write(yes) ; write(no) ), nl.
main.
