:- initialization(main).
main :- ( char_type(' ', space) -> write(yes) ; write(no) ), nl,
        ( char_type(a, alnum) -> write(yes) ; write(no) ), nl,
        ( char_type('3', alnum) -> write(yes) ; write(no) ), nl.
