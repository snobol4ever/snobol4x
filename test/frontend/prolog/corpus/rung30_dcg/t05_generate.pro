:- initialization(main).

item(X) --> [X].

% Use findall to collect all parses
main :-
    findall(X, phrase(item(X), [a]), As),
    write(As), nl,
    findall(X, phrase(item(X), [b]), Bs),
    write(Bs), nl.
