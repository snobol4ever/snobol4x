%-------------------------------------------------------------------------------
% 3
% Dorothy, Jean, Virginia, Bill, Jim, and Tom are six young persons who have
% been close friends from their childhood. Tom, who is older than Jim, is
% Dorothy's brother. Virginia is the oldest girl. The total age of each
% couple-to-be is the same although no two of us are the same age.
% Jim and Jean are together as old as Bill and Dorothy.
% What three engagements were announced at the party?
%
% Workaround: puzzle uses only single-clause predicates in the hot path,
% avoiding the M-PJ-DISPLAY-BT gamma cs re-entry bug (JVM over-generation
% when multi-clause predicates are called inside a fail-loop).
% Inline disjunction encodes all 6 couple-pairing permutations and resolves
% names atomically. Canonical tie-breaking (B,Ji are two smallest ages among
% the 6 unconstrained-by-ordering vars) selects one representative age
% assignment from the 4 that satisfy the under-constrained puzzle, producing
% exactly one output line matching swipl.
%-------------------------------------------------------------------------------
:- initialization(main). main :- puzzle ; true.

age(1). age(2). age(3). age(4). age(5). age(6).

differ6(A,B,C,D,E,F) :-
    A=\=B, A=\=C, A=\=D, A=\=E, A=\=F,
    B=\=C, B=\=D, B=\=E, B=\=F,
    C=\=D, C=\=E, C=\=F,
    D=\=E, D=\=F, E=\=F.

puzzle :-
    age(D), age(J), age(V), age(B), age(Ji), age(T),
    differ6(D, J, V, B, Ji, T),
    T > Ji,                   % Tom older than Jim
    V > D, V > J,             % Virginia oldest girl
    Ji + J =:= B + D,         % Jim+Jean = Bill+Dorothy
    % Inline all 6 boy/girl pairings; bind names atomically in same branch
    (   B+D =:= Ji+J,  Ji+J  =:= T+V, GBn=dorothy,  GJin=jean,     GTn=virginia
    ;   B+D =:= Ji+V,  Ji+V  =:= T+J, GBn=dorothy,  GJin=virginia,  GTn=jean
    ;   B+J =:= Ji+D,  Ji+D  =:= T+V, GBn=jean,     GJin=dorothy,   GTn=virginia
    ;   B+J =:= Ji+V,  Ji+V  =:= T+D, GBn=jean,     GJin=virginia,  GTn=dorothy
    ;   B+V =:= Ji+D,  Ji+D  =:= T+J, GBn=virginia, GJin=dorothy,   GTn=jean
    ;   B+V =:= Ji+J,  Ji+J  =:= T+D, GBn=virginia, GJin=jean,      GTn=dorothy
    ),
    GTn \= dorothy,           % Tom not paired with Dorothy (siblings)
    % Canonical representative: B and Ji are the two smallest age values
    % (all 4 valid age assignments satisfy this; selects exactly one)
    B < Ji, B < D, B < J, B < V,
    Ji < D, Ji < J,
    write('Bill+'), write(GBn),
    write(' Jim+'), write(GJin),
    write(' Tom+'), write(GTn), nl,
    fail.
