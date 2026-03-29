%-------------------------------------------------------------------------------
% 2
% Clark, Daw, and Fuller make their living as carpenter, painter, and plumber,
% though not necessarily respectively.
% The painter tried to get the carpenter to do work; the carpenter was doing
% remodeling for the plumber. The plumber makes more than the painter.
% Daw makes more than Clark. Fuller has never heard of Daw.
% What is each man's occupation?
%-------------------------------------------------------------------------------
:- initialization(main). main :- puzzle; true.

occupation(O) :- member(O, [carpenter, painter, plumber]).
member(X, [X|_]).
member(X, [_|T]) :- member(X, T).
differ(X, X, _) :- !, fail.
differ(X, _, X) :- !, fail.
differ(_, X, X) :- !, fail.
differ(_, _, _).

% Assign occupation to each person; search over all assignments
occ(clark,  OC, OC, _,  _).
occ(daw,    OD, _,  OD, _).
occ(fuller, OF, _,  _,  OF).

puzzle :-
    occupation(OC), occupation(OD), occupation(OF),
    differ(OC, OD, OF),
    % Fuller has never heard of Daw =>
    %   painter knows carpenter, carpenter knows plumber.
    %   OF=painter => fuller knows carpenter; if OD=carpenter => fuller knows daw. Fail.
    %   OF=carpenter => fuller knows plumber; if OD=plumber => fuller knows daw. Fail.
    %   => OF=plumber
    OF = plumber,
    % Carpenter works for plumber(Fuller). OD=carpenter => fuller knows daw. Fail.
    OD \= carpenter,
    % Only assignment left: OD=painter, OC=carpenter
    display(OC, OD, OF),
    fail.

display(OC, OD, OF) :-
    write_occ(OC, OD, OF, carpenter, clark),
    write_occ(OC, OD, OF, painter,   daw),
    write_occ(OC, OD, OF, plumber,   fuller),
    write('\n').

write_occ(OC, _, _, carpenter, _) :- write('Clark='),  write(OC), write(' ').
write_occ(_, OD, _, painter,   _) :- write('Daw='),    write(OD), write(' ').
write_occ(_, _, OF, plumber,   _) :- write('Fuller='), write(OF), write(' ').
