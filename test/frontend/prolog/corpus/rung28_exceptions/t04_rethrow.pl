:- initialization(main).
main :-
    catch(inner, E, (write(outer), write(' '), write(E), nl)).
inner :-
    catch(throw(mine), other, write(wrong)).
