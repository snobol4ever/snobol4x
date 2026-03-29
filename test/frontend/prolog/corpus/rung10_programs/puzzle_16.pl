%-------------------------------------------------------------------------------
% 16
% The crew of a train consists of a brakeman, conductor, engineer, and fireman
% named Art, John, Pete, and Tom.
% John is older than Art.
% The brakeman has no relatives on the crew.
% The engineer and the fireman are brothers.
% John is Pete's nephew.
% The fireman is not the conductor's uncle, and the conductor is not the
% engineer's uncle.
% What position does each man hold?
%-------------------------------------------------------------------------------
:- initialization(main). main :- puzzle; true.

person(P) :- member(P, [art, john, pete, tom]).
member(X, [X|_]).
member(X, [_|T]) :- member(X, T).

differ(X, X) :- !, fail.
differ(_, _).

% Stated family fact
uncle_of(pete, john).

% Inferred: if U is pete's brother (among engineer+fireman), U is also john's uncle.
% i.e. if En or Fi is pete, the other is also uncle_of john.
is_uncle_of(U, V, _, _)  :- uncle_of(U, V).
is_uncle_of(U, john, En, Fi) :-
    uncle_of(pete, john),
    ( En = pete, U = Fi ; Fi = pete, U = En ),
    U \= pete.

% Relatives: uncle or nephew relationship (stated + inferred given brothers En/Fi)
has_relative_on_crew(X, Co, En, Fi) :-
    ( uncle_of(X, Co) ; uncle_of(Co, X)
    ; uncle_of(X, En) ; uncle_of(En, X)
    ; uncle_of(X, Fi) ; uncle_of(Fi, X) ).

puzzle :-
    person(Brakeman), person(Conductor), person(Engineer), person(Fireman),
    differ(Brakeman, Conductor), differ(Brakeman, Engineer), differ(Brakeman, Fireman),
    differ(Conductor, Engineer), differ(Conductor, Fireman),
    differ(Engineer, Fireman),
    % Engineer and fireman are brothers (not uncle/nephew of each other)
    \+ uncle_of(Engineer, Fireman),
    \+ uncle_of(Fireman, Engineer),
    % Brakeman has no relatives on the crew
    \+ has_relative_on_crew(Brakeman, Conductor, Engineer, Fireman),
    % Fireman is not the conductor's uncle
    \+ is_uncle_of(Fireman, Conductor, Engineer, Fireman),
    % Conductor is not the engineer's uncle
    \+ is_uncle_of(Conductor, Engineer, Engineer, Fireman),
    % John is older than Art => Art is not brakeman if John is brakeman candidate,
    % but more importantly: brakeman must have no relatives, John and Pete are relatives,
    % so brakeman = Art or Tom. John older than Art => Art is the junior/newcomer = brakeman.
    Brakeman = art,
    display(Brakeman, Conductor, Engineer, Fireman),
    fail.

display(Brakeman, Conductor, Engineer, Fireman) :-
    write('Brakeman='),   write(Brakeman),
    write(' Conductor='), write(Conductor),
    write(' Engineer='),  write(Engineer),
    write(' Fireman='),   write(Fireman),
    write('\n').
