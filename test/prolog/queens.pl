% queens.pro — N-Queens via backtracking
% Prolog idiom: generate-and-test, constraint propagation via \+
% Shows: recursive generate, list membership, arithmetic constraints
:- initialization(main, main).

queens(N, Qs) :-
    length(Qs, N),
    board(Qs, Bs, 0, N, _, _),
    permutation(Bs, Qs).

board([], [], N, N, _, _).
board([_|Qs], [B|Bs], B0, N, [B0|Ups], [B0|Downs]) :-
    B1 is B0 + 1,
    board(Qs, Bs, B1, N, Ups, Downs).

permutation([], []).
permutation(Xs, [X|Ys]) :-
    select(X, Xs, Rest),
    permutation(Rest, Ys).

safe([]).
safe([Q|Qs]) :-
    no_attack(Q, Qs, 1),
    safe(Qs).

no_attack(_, [], _).
no_attack(Q, [Q1|Qs], D) :-
    Q1 - Q =\= D,
    Q - Q1 =\= D,
    D1 is D + 1,
    no_attack(Q, Qs, D1).

solution(N, Qs) :-
    numlist(1, N, Ns),
    permutation(Ns, Qs),
    safe(Qs).

main :-
    N = 6,
    format("~w-Queens solutions:~n", [N]),
    aggregate_all(count, solution(N, _), Count),
    format("  ~w solutions~n", [Count]),
    once(solution(N, Q)),
    format("  first: ~w~n", [Q]).
