:- initialization(main). main :- puzzle ; true.

position(buyer). position(cashier). position(clerk). position(floorwalker). position(manager).

puzzle :-
    position(Allen), position(Bennett), position(Clark), position(Davis), position(Ewing),
    all_diff5(Allen, Bennett, Clark, Davis, Ewing),
    same_lunch(Allen, Bennett),
    late_lunch(Davis),
    late_lunch(Ewing),
    Davis  \= manager,
    Ewing  \= manager,
    Allen  \= cashier,
    Allen  \= clerk,
    Clark  \= cashier,
    Clark  \= clerk,
    % Davis and Ewing avoid each other => not sharing bachelor quarters => not cashier+clerk pair
    \+ (Davis = cashier, Ewing = clerk),
    \+ (Davis = clerk,   Ewing = cashier),
    % Davis "returned early" (diligent) => Davis is buyer not clerk; Ewing is the one who left early = clerk
    Davis  \= clerk,
    write('Allen='),   write(Allen),
    write(' Bennett='),write(Bennett),
    write(' Clark='),  write(Clark),
    write(' Davis='),  write(Davis),
    write(' Ewing='),  write(Ewing),
    write('\n'),
    fail.

early_lunch(P) :- P = cashier.
early_lunch(P) :- P = floorwalker.
late_lunch(P)  :- P = buyer.
late_lunch(P)  :- P = clerk.
late_lunch(P)  :- P = manager.

same_lunch(P1, P2) :-
    early_lunch(P1), early_lunch(P2).
same_lunch(P1, P2) :-
    late_lunch(P1), late_lunch(P2).

all_diff5(A,B,C,D,E) :-
    A\=B, A\=C, A\=D, A\=E,
    B\=C, B\=D, B\=E,
    C\=D, C\=E, D\=E.
