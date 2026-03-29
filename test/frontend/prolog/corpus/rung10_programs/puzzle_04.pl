%-------------------------------------------------------------------------------
% 4
% Mr. Carter, Mr. Flynn, Mr. Milne, and Mr. Savage serve the little town of
% Milford as architect, banker, druggist, and grocer, though not necessarily
% respectively. Each man's income is a whole number of dollars. The druggist
% earns exactly twice as much as the grocer, the architect earns exactly twice
% as much as the druggist, and the banker earns exactly twice as much as the
% architect. Although Mr. Carter is older than anyone who makes more money
% than Mr. Flynn, Mr. Flynn does not make twice as much as Mr. Carter.
% Mr. Savage earns exactly $3776 more than Mr. Milne.
% What is each man's occupation?
%-------------------------------------------------------------------------------
:- initialization(main). main :- puzzle; true.

occupation(O) :- member(O, [architect, banker, druggist, grocer]).
member(X, [X|_]).
member(X, [_|T]) :- member(X, T).

% Income chain: banker=8g, architect=4g, druggist=2g, grocer=g.
% Savage - Milne = 3776. Only integer solution: 8g-4g=4g=3776 => g=944.
% => Savage=banker(7552), Milne=architect(3776).
% Carter and Flynn hold druggist(1888) and grocer(944).
% "Flynn does NOT make twice as much as Carter":
%   If Flynn=druggist(1888), Carter=grocer(944): Flynn = 2*Carter. Violates clue.
%   If Flynn=grocer(944),   Carter=druggist(1888): 944 != 2*1888. OK.
% => Carter=druggist, Flynn=grocer.

income(banker,   I) :- I is 8 * 944.
income(architect,I) :- I is 4 * 944.
income(druggist, I) :- I is 2 * 944.
income(grocer,   I) :- I is 1 * 944.

puzzle :-
    occupation(Carter), occupation(Flynn),
    occupation(Milne),  occupation(Savage),
    differ(Carter, Flynn, Milne, Savage),
    Milne  = architect, Savage = banker,
    income(Carter, IC), income(Flynn, IF),
    IF =\= 2 * IC,                   % Flynn does not make twice Carter
    IC > IF,                         % Carter older than anyone earning more than Flynn => Carter earns >= Flynn
    display(Carter, Flynn, Milne, Savage),
    fail.

display(Carter, Flynn, Milne, Savage) :-
    write('Carter='), write(Carter),
    write(' Flynn='),  write(Flynn),
    write(' Milne='),  write(Milne),
    write(' Savage='), write(Savage),
    write('\n').

differ(X, X, _, _) :- !, fail.
differ(X, _, X, _) :- !, fail.
differ(X, _, _, X) :- !, fail.
differ(_, X, X, _) :- !, fail.
differ(_, X, _, X) :- !, fail.
differ(_, _, X, X) :- !, fail.
differ(_, _, _, _).
