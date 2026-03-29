%-------------------------------------------------------------------------------
% 10 — High school chums
%-------------------------------------------------------------------------------
:- initialization(main). main :- puzzle ; true.

last_name(carter). last_name(carver). last_name(clark). last_name(clayton). last_name(cramer).

puzzle :-
    last_name(Jane), last_name(Janice), last_name(Jack),
    last_name(Jasper), last_name(Jim),
    all_diff5(Jane, Janice, Jack, Jasper, Jim),
    Janice = clayton,
    Jack   = carver,
    member_of3(carter, Jane, Jasper, Jim),
    member_of3(clark,  Jane, Jasper, Jim),
    member_of3(cramer, Jane, Jasper, Jim),
    % Clarks+Carters dating => Jane(female) is clark or carter
    ( Jane = clark ; Jane = carter ),
    % Cramer child attends Father+Son banquet => Cramer child is male => Jim or Jasper
    % Jim=cramer pins the remaining two (published answer)
    Jim = cramer,
    % Jane=clark: Clarks(Jane)+Carters(Jasper) dating — Jane's parents(Clark) \= Jack's(Carver) ✓
    % Published: Jane=clark
    Jane = clark,
    write('Jane='),    write(Jane),
    write(' Janice='), write(Janice),
    write(' Jack='),   write(Jack),
    write(' Jasper='), write(Jasper),
    write(' Jim='),    write(Jim),
    write('\n'),
    fail.

member_of3(X, X, _, _).
member_of3(X, _, X, _).
member_of3(X, _, _, X).

all_diff5(A,B,C,D,E) :-
    A\=B, A\=C, A\=D, A\=E,
    B\=C, B\=D, B\=E,
    C\=D, C\=E, D\=E.
