%-------------------------------------------------------------------------------
% 19
% Allen, Brady, McCoy, and Smith have offices on different floors of the same
% 18-story building. One is an accountant, one an architect, one a dentist,
% one a lawyer. Floors 1-18.
% Allen's office: McCoy < Allen < Smith.
% Brady's office is below the dentist's.
% Smith's floor = 5 * lawyer's floor.
% Architect + 2 floors = halfway between dentist and accountant.
% Architect / 2 floors = halfway between dentist and lawyer.
% What is each man's profession and floor?
%-------------------------------------------------------------------------------
:- initialization(main). main :- puzzle; true.

person(P) :- member(P, [allen, brady, mccoy, smith]).
profession(R) :- member(R, [accountant, architect, dentist, lawyer]).

member(X, [X|_]).
member(X, [_|T]) :- member(X, T).

differ(X, X) :- !, fail.
differ(_, _).

floor(F) :- between(1, 18, F).

puzzle :-
    % Assign professions
    profession(PAl), profession(PBr), profession(PMc), profession(PSm),
    differ(PAl, PBr), differ(PAl, PMc), differ(PAl, PSm),
    differ(PBr, PMc), differ(PBr, PSm),
    differ(PMc, PSm),
    % Assign floors
    floor(FAl), floor(FBr), floor(FMc), floor(FSm),
    differ(FAl, FBr), differ(FAl, FMc), differ(FAl, FSm),
    differ(FBr, FMc), differ(FBr, FSm),
    differ(FMc, FSm),
    % Allen between McCoy and Smith
    FMc < FAl, FAl < FSm,
    % Brady below dentist
    floor_of(dentist, PAl, PBr, PMc, PSm, FAl, FBr, FMc, FSm, FDe),
    FBr < FDe,
    % Brady \= dentist
    PBr \= dentist,
    % Smith = 5 * lawyer
    floor_of(lawyer, PAl, PBr, PMc, PSm, FAl, FBr, FMc, FSm, FLaw),
    FSm =:= 5 * FLaw,
    % Architect + 2 = (dentist + accountant) / 2
    floor_of(architect,  PAl, PBr, PMc, PSm, FAl, FBr, FMc, FSm, FArch),
    floor_of(accountant, PAl, PBr, PMc, PSm, FAl, FBr, FMc, FSm, FAcc),
    (FArch + 2) * 2 =:= FDe + FAcc,
    % Architect / 2 = (dentist + lawyer) / 2
    FArch * 1 =:= FDe + FLaw,  % arch/2 = (dent+law)/2  =>  arch = dent+law
    display(PAl, FAl, PBr, FBr, PMc, FMc, PSm, FSm),
    fail.

% floor_of(Profession, ..., Floor)
floor_of(Prof, Prof, _,    _,    _,    F,   _,   _,   _,   F).
floor_of(Prof, _,    Prof, _,    _,    _,   F,   _,   _,   F).
floor_of(Prof, _,    _,    Prof, _,    _,   _,   F,   _,   F).
floor_of(Prof, _,    _,    _,    Prof, _,   _,   _,   F,   F).

display(PAl, FAl, PBr, FBr, PMc, FMc, PSm, FSm) :-
    write('Allen='),  write(PAl), write('('), write(FAl), write(')'),
    write(' Brady='), write(PBr), write('('), write(FBr), write(')'),
    write(' McCoy='), write(PMc), write('('), write(FMc), write(')'),
    write(' Smith='), write(PSm), write('('), write(FSm), write(')'),
    write('\n').
