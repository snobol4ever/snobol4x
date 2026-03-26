:- initialization(main).
main :-
    catch(safe(3), _, write(bad)),
    write(ok), nl.
safe(X) :- X > 0, write(X), nl.
