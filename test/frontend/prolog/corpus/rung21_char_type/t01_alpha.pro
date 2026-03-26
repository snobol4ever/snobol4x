:- initialization(main).
main :- ( char_type(a, alpha) -> write(yes) ; write(no) ), nl,
        ( char_type('3', alpha) -> write(yes) ; write(no) ), nl.
