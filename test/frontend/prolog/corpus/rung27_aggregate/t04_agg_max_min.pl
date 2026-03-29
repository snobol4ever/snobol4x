:- initialization(main).
val(5). val(3). val(8). val(1).
main :-
    aggregate_all(max(V), val(V), Max), write(Max), nl,
    aggregate_all(min(W), val(W), Min), write(Min), nl.
