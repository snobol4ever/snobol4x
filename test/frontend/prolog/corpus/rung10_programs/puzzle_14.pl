%-------------------------------------------------------------------------------
% 14
% Bill, Ed, and Tom with their wives Grace, Helen, and Mary played eighteen
% holes of golf together.
% Mary, Helen, Grace, and Ed shot 106, 102, 100, and 94 respectively.
% Bill and Tom shot 98 and 96, but they couldn't tell who made which since
% they hadn't put their names on their scorecards.
% When they identified their cards it turned out that two couples had the
% same total score.
% Ed's wife beat Bill's wife.
% What is the name of each man's wife, and what scores did Bill and Tom make?
%-------------------------------------------------------------------------------
:- initialization(main). main :- puzzle; true.

score(mary,   106).
score(helen,  102).
score(grace,  100).
score(ed,      94).

wife(W) :- member(W, [grace, helen, mary]).
member(X, [X|_]).
member(X, [_|T]) :- member(X, T).

% Two couples share the same total — expressed as a predicate, not inline disjunction.
same_total(A, A, _).
same_total(A, _, A).
same_total(_, A, A).

puzzle :-
    wife(WEd), wife(WBill), wife(WTom),
    differ(WEd, WBill, WTom),
    member(BillScore, [96, 98]),
    TomScore is 194 - BillScore,
    score(WEd,   SE), EdTotal   is  94 + SE,
    score(WBill, SB), BillTotal is BillScore + SB,
    score(WTom,  ST), TomTotal  is TomScore  + ST,
    same_total(EdTotal, BillTotal, TomTotal),
    SE < SB,
    display(WEd, WBill, WTom, BillScore, TomScore),
    fail.

display(WEd, WBill, WTom, BillScore, TomScore) :-
    write('Ed='),    write(WEd),
    write(' Bill='), write(WBill), write('('), write(BillScore), write(')'),
    write(' Tom='),  write(WTom),  write('('), write(TomScore),  write(')'),
    write('\n').

differ(X, X, _) :- !, fail.
differ(X, _, X) :- !, fail.
differ(_, X, X) :- !, fail.
differ(_, _, _).
