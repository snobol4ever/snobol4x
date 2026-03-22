% rung06_lists — append/3, length/2, reverse/2
% Expected output: [a,b,c,d]  4  [d,c,b,a]
:- initialization(main).
append([], L, L).
append([H|T], L, [H|R]) :- append(T, L, R).
length([], 0).
length([_|T], N) :- length(T, N1), N is N1 + 1.
reverse([], []).
reverse([H|T], R) :- reverse(T, RT), append(RT, [H], R).
main :-
    append([a,b], [c,d], L), write(L), nl,
    length([a,b,c,d], N), write(N), nl,
    reverse([a,b,c,d], R), write(R), nl.
