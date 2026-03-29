%-------------------------------------------------------------------------------
% 17
% Ed, Frank, George, and Harry took their wives to the Country Club dance.
% At one point: Betty was dancing with Ed, Alice was dancing with Carol's
% husband, Dorothy was dancing with Alice's husband, Frank was dancing with
% George's wife, and George was dancing with Ed's wife.
% What is the name of each man's wife, and with whom was each man dancing?
%-------------------------------------------------------------------------------
:- initialization(main). main :- puzzle; true.

wife(Wife) :- member(Wife, [alice, betty, carol, dorothy]).
member(X, [X|_]).
member(X, [_|T]) :- member(X, T).

% husband_of(Wife, WEd, WFrank, WGeorge, WHarry, Husband)
husband_of(W, W, _, _, _, ed).
husband_of(W, _, W, _, _, frank).
husband_of(W, _, _, W, _, george).
husband_of(W, _, _, _, W, harry).

% dance_partner(Man, WEd, WFrank, WGeorge, Partner)
% Fixed by clues: Ed<->betty, Frank<->WGeorge, George<->WEd; Harry gets remainder
dance_partner(ed,     _,       _,       _,       betty).
dance_partner(frank,  _,       _,       WGeorge, WGeorge).
dance_partner(george, WEd,     _,       _,       WEd).
dance_partner(harry,  WEd,     WFrank,  WGeorge, P) :-
    member(P, [alice, betty, carol, dorothy]),
    differ(P, betty), differ(P, WGeorge), differ(P, WEd).

puzzle :-
    wife(WEd), wife(WFrank), wife(WGeorge), wife(WHarry),
    differ(WEd, WFrank, WGeorge, WHarry),
    differ(WEd, betty),                      % George dances with WEd, Ed with betty => WEd \= betty
    husband_of(carol, WEd, WFrank, WGeorge, WHarry, HCarol),
    husband_of(alice, WEd, WFrank, WGeorge, WHarry, HAlice),
    dance_partner(HCarol, WEd, WFrank, WGeorge, alice),    % Alice dances with Carol's husband
    dance_partner(HAlice, WEd, WFrank, WGeorge, dorothy),  % Dorothy dances with Alice's husband
    display(WEd, WFrank, WGeorge, WHarry),
    fail.

display(WEd, WFrank, WGeorge, WHarry) :-
    write('Ed='),     write(WEd),
    write(' Frank='), write(WFrank),
    write(' George='), write(WGeorge),
    write(' Harry='), write(WHarry),
    write('\n').

differ(X, X, _, _) :- !, fail.
differ(X, _, X, _) :- !, fail.
differ(X, _, _, X) :- !, fail.
differ(_, X, X, _) :- !, fail.
differ(_, X, _, X) :- !, fail.
differ(_, _, X, X) :- !, fail.
differ(_, _, _, _).

differ(X, X) :- !, fail.
differ(_, _).
