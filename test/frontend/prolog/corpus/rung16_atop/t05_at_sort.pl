main :-
    ( apple @< banana -> write(ok1) ; write(fail1) ), nl,
    ( zebra @> mango  -> write(ok2) ; write(fail2) ), nl,
    ( cat @=< cat     -> write(ok3) ; write(fail3) ), nl,
    ( dog @>= cat     -> write(ok4) ; write(fail4) ), nl.
main.
