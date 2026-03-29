%-------------------------------------------------------------------------------
% 20
% Adams, Brown, Clark, and Davis: historian, poet, novelist, playwright.
% Each reads a book by one of the others (not own). Adams+Brown exchanged.
% Brown brought Davis's book. Poet reads a play. Novelist never read history.
%-------------------------------------------------------------------------------
:- initialization(main). main :- puzzle; true.

person(P) :- member(P, [adams, brown, clark, davis]).
profession(R) :- member(R, [historian, poet, novelist, playwright]).
member(X,[X|_]). member(X,[_|T]) :- member(X,T).
differ(X,X) :- !,fail. differ(_,_).

% author_profession(Author, PrAd, PrBr, PrCl, PrDa, Profession)
author_profession(adams, Pr, _,  _,  _,  Pr).
author_profession(brown, _,  Pr, _,  _,  Pr).
author_profession(clark, _,  _,  Pr, _,  Pr).
author_profession(davis, _,  _,  _,  Pr, Pr).

puzzle :-
    profession(PrAd), profession(PrBr), profession(PrCl), profession(PrDa),
    differ(PrAd,PrBr), differ(PrAd,PrCl), differ(PrAd,PrDa),
    differ(PrBr,PrCl), differ(PrBr,PrDa), differ(PrCl,PrDa),
    % Brown brought Davis's book; Adams+Brown exchanged => Adams reads Davis, Brown reads Adams
    RdAd = davis, RdBr = adams,
    % Clark and Davis read brown or clark (the two not taken)
    person(RdCl), person(RdDa),
    differ(RdCl, clark), differ(RdDa, davis),
    differ(RdCl, RdDa),
    differ(RdCl, RdAd), differ(RdCl, RdBr),
    differ(RdDa, RdAd), differ(RdDa, RdBr),
    % Poet reads playwright's book
    author_profession(RdAd, PrAd,PrBr,PrCl,PrDa, GenAd),
    author_profession(RdBr, PrAd,PrBr,PrCl,PrDa, GenBr),
    author_profession(RdCl, PrAd,PrBr,PrCl,PrDa, GenCl),
    author_profession(RdDa, PrAd,PrBr,PrCl,PrDa, GenDa),
    ( PrAd = poet -> GenAd = playwright ; true ),
    ( PrBr = poet -> GenBr = playwright ; true ),
    ( PrCl = poet -> GenCl = playwright ; true ),
    ( PrDa = poet -> GenDa = playwright ; true ),
    % Novelist doesn't read historian's book
    ( PrAd = novelist -> GenAd \= historian ; true ),
    ( PrBr = novelist -> GenBr \= historian ; true ),
    ( PrCl = novelist -> GenCl \= historian ; true ),
    ( PrDa = novelist -> GenDa \= historian ; true ),
    display(PrAd,RdAd,PrBr,RdBr,PrCl,RdCl,PrDa,RdDa),
    fail.

display(PrAd,RdAd,PrBr,RdBr,PrCl,RdCl,PrDa,RdDa) :-
    write('adams='), write(PrAd), write(' reads='), write(RdAd), write('\n'),
    write('brown='), write(PrBr), write(' reads='), write(RdBr), write('\n'),
    write('clark='), write(PrCl), write(' reads='), write(RdCl), write('\n'),
    write('davis='), write(PrDa), write(' reads='), write(RdDa), write('\n').
