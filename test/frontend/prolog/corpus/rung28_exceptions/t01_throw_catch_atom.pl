:- initialization(main).
main :-
    catch(throw(hello), E, (write(caught), write(' '), write(E), nl)).
