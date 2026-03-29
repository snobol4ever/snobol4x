main :-
    ( b @>= a -> write(yes) ; write(no) ), nl,
    ( b @>= b -> write(yes) ; write(no) ), nl,
    ( a @>= b -> write(yes) ; write(no) ), nl.
main.
