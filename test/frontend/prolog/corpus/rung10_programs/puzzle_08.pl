%-------------------------------------------------------------------------------
% 8 — Department store positions
%-------------------------------------------------------------------------------
:- initialization(main). main :- puzzle ; true.

position(buyer). position(cashier). position(clerk). position(floorwalker). position(manager).

puzzle :-
    position(Ames), position(Brown), position(Conroy), position(Davis), position(Evans),
    all_diff5(Ames, Brown, Conroy, Davis, Evans),
    % buyer is a bachelor (male, unmarried): women and Conroy(married) excluded
    Ames   \= buyer,
    Brown  \= buyer,
    Conroy \= buyer,
    % Conroy married => not cashier (to marry clerk) and not clerk (to marry cashier)
    Conroy \= cashier,
    Conroy \= clerk,
    % Manager refused Conroy a raise => Conroy \= manager
    Conroy \= manager,
    % Davis is best man at clerk+cashier wedding => Davis \= clerk, Davis \= cashier
    Davis  \= clerk,
    Davis  \= cashier,
    % Cashier and manager were college roommates => same sex
    cashier_manager_same_sex(Ames, Brown, Conroy, Davis, Evans),
    % Clerk marries cashier => opposite sex
    clerk_cashier_opp(Ames, Brown, Conroy, Davis, Evans),
    % Evans and Ames only business contacts => not the marrying pair
    \+ (Evans = clerk, Ames = cashier),
    \+ (Ames  = clerk, Evans = cashier),
    write('Ames='),   write(Ames),
    write(' Brown='), write(Brown),
    write(' Conroy='),write(Conroy),
    write(' Davis='), write(Davis),
    write(' Evans='), write(Evans),
    write('\n'),
    fail.

sex(ames, f). sex(brown, f).
sex(conroy, m). sex(davis, m). sex(evans, m).

holder_sex(Pos, Ames, Brown, Conroy, Davis, Evans, Sex) :-
    ( Ames   = Pos -> sex(ames,   Sex)
    ; Brown  = Pos -> sex(brown,  Sex)
    ; Conroy = Pos -> sex(conroy, Sex)
    ; Davis  = Pos -> sex(davis,  Sex)
    ; Evans  = Pos -> sex(evans,  Sex)
    ).

cashier_manager_same_sex(A,B,C,D,E) :-
    holder_sex(cashier, A,B,C,D,E, S1),
    holder_sex(manager, A,B,C,D,E, S2),
    S1 = S2.

clerk_cashier_opp(A,B,C,D,E) :-
    holder_sex(clerk,   A,B,C,D,E, S1),
    holder_sex(cashier, A,B,C,D,E, S2),
    S1 \= S2.

all_diff5(A,B,C,D,E) :-
    A\=B, A\=C, A\=D, A\=E,
    B\=C, B\=D, B\=E,
    C\=D, C\=E, D\=E.
