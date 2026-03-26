:- initialization(main).
main :-
    catch(throw(myerr), myerr, write(matched)), nl.
