/* plunit.pl — minimal plunit shim for snobol4x Prolog JVM backend
 * No call/N. Goals executed directly. Single-clause dispatch avoids
 * ucall retry loops from multi-clause predicates.
 */

begin_tests(_).
end_tests(_).

member(X, [X|_]).
member(X, [_|T]) :- member(X, T).

memberchk(X, [X|_]) :- !.
memberchk(X, [_|T]) :- memberchk(X, T).

forall(Cond, Action) :- \+ forall_fails(Cond, Action).
forall_fails(Cond, Action) :- Cond, \+ Action.

acyclic_term(_).
cyclic_term(_) :- fail.

maplist(_, []).
maplist(Goal, [H|T]) :- call_1(Goal, H), maplist(Goal, T).
maplist(_, [], []).
maplist(Goal, [H1|T1], [H2|T2]) :- call_2(Goal, H1, H2), maplist(Goal, T1, T2).

call_1(Goal, Arg) :- Goal =.. L, append(L, [Arg], L2), G2 =.. L2, G2.
call_2(Goal, A1, A2) :- Goal =.. L, append(L, [A1,A2], L2), G2 =.. L2, G2.

append([], L, L).
append([H|T], L, [H|R]) :- append(T, L, R).

%% counters
pj_init :- nb_setval(pj_p,0), nb_setval(pj_f,0), nb_setval(pj_s,0).
pj_inc_pass :- nb_getval(pj_p,N), N1 is N+1, nb_setval(pj_p,N1).
pj_inc_fail :- nb_getval(pj_f,N), N1 is N+1, nb_setval(pj_f,N1).
pj_inc_skip :- nb_getval(pj_s,N), N1 is N+1, nb_setval(pj_s,N1).
pj_summary :-
    nb_getval(pj_p,P), nb_getval(pj_f,F), nb_getval(pj_s,S),
    format('~n% ~w passed, ~w failed, ~w skipped~n',[P,F,S]).

%% run_tests
run_tests :- pj_init, run_all_suites, pj_summary.
run_tests(Suite) :- pj_init, run_suite(Suite), pj_summary.

run_all_suites :- pj_suite(S), run_suite(S), fail.
run_all_suites.

run_suite(Suite) :-
    format('~n% PL-Unit: ~w~n',[Suite]), !,
    run_suite_tests(Suite).

run_suite_tests(Suite) :-
    pj_test(Suite, Name, Opts, Goal),
    run_one(Suite, Name, Opts, Goal),
    fail.
run_suite_tests(_).

%% run_one: single-clause, no multi-predicate dispatch
run_one(Suite, Name, Opts, Goal) :-
    ( pj_has_sto(Opts) ->
        pj_inc_skip, format('  skip: ~w:~w  [sto]~n',[Suite,Name])
    ; pj_skip_condition(Opts) ->
        pj_inc_skip, format('  skip: ~w:~w  [condition]~n',[Suite,Name])
    ; pj_has_error(Opts, ExpErr) ->
        run_error(Suite, Name, Goal, ExpErr)
    ; pj_has_throws(Opts, ExpThrow) ->
        run_throw(Suite, Name, Goal, ExpThrow)
    ; pj_wants_fail(Opts) ->
        run_fail(Suite, Name, Goal)
    ; pj_has_true(Opts, Expr) ->
        run_true(Suite, Name, Goal, Expr)
    ; pj_has_all(Opts, AllExpr) ->
        run_all(Suite, Name, Goal, AllExpr)
    ;
        run_succeed(Suite, Name, Goal, Opts)
    ).

%% option tests — all deterministic, no backtracking needed
pj_has_sto([H|_]) :- H = sto(_), !.
pj_has_sto([_|T]) :- pj_has_sto(T).

pj_skip_condition(Opts) :-
    member(condition(C), Opts), \+ C.

pj_has_error([error(E)|_], E) :- !.
pj_has_error([_|T], E) :- pj_has_error(T, E).
pj_has_error(error(E), E).

pj_has_throws([throws(T)|_], T) :- !.
pj_has_throws([_|T2], T) :- pj_has_throws(T2, T).
pj_has_throws(throws(T), T).

pj_wants_fail([fail|_]) :- !.
pj_wants_fail([false|_]) :- !.
pj_wants_fail([_|T]) :- pj_wants_fail(T).
pj_wants_fail(fail).
pj_wants_fail(false).

pj_has_true([true(E)|_], E) :- !.
pj_has_true([_|T], E) :- pj_has_true(T, E).

pj_has_all([all(E)|_], E) :- !.
pj_has_all([_|T], E) :- pj_has_all(T, E).

%% test runners — use catch with Goal directly (no call/1)
run_succeed(Suite, Name, Goal, Opts) :-
    ( is_list(Opts), \+ Opts = [] -> true ; Opts = [] ; Opts = true ),
    ( catch(Goal, _E, fail) ->
        pj_inc_pass, format('  pass: ~w:~w~n',[Suite,Name])
    ;   pj_inc_fail, format('  FAIL: ~w:~w  (goal failed)~n',[Suite,Name])
    ).
run_succeed(Suite, Name, Goal, Opts) :-
    Opts \= [], \+ is_list(Opts), Opts \= true, Opts \= fail, Opts \= false,
    run_true(Suite, Name, Goal, Opts).

run_fail(Suite, Name, Goal) :-
    ( catch(Goal, _E, true) ->
        pj_inc_fail, format('  FAIL: ~w:~w  (expected fail, succeeded)~n',[Suite,Name])
    ;   pj_inc_pass, format('  pass: ~w:~w~n',[Suite,Name])
    ).

run_error(Suite, Name, Goal, ExpErr) :-
    ( catch(Goal, error(ActErr,_), pj_match_err(Suite,Name,ExpErr,ActErr)) ->
        true
    ;   pj_inc_fail, format('  FAIL: ~w:~w  (no exception)~n',[Suite,Name])
    ).

pj_match_err(Suite, Name, Exp, Act) :-
    copy_term(Exp, ExpC),
    ( ExpC = Act ->
        pj_inc_pass, format('  pass: ~w:~w~n',[Suite,Name])
    ; functor(ExpC,F,_), functor(Act,F,_) ->
        pj_inc_pass, format('  pass: ~w:~w~n',[Suite,Name])
    ;   pj_inc_fail, format('  FAIL: ~w:~w  (error mismatch ~w vs ~w)~n',[Suite,Name,Exp,Act])
    ).

run_throw(Suite, Name, Goal, ExpThrow) :-
    ( catch(Goal, Actual,
        ( Actual = ExpThrow ->
            pj_inc_pass, format('  pass: ~w:~w~n',[Suite,Name])
        ;   pj_inc_fail, format('  FAIL: ~w:~w  (throw mismatch)~n',[Suite,Name])
        )) ->
        true
    ;   pj_inc_fail, format('  FAIL: ~w:~w  (no throw)~n',[Suite,Name])
    ).

run_true(Suite, Name, Goal, Expr) :-
    ( catch(Goal, _E, fail) ->
        ( catch(Expr, _E2, fail) ->
            pj_inc_pass, format('  pass: ~w:~w~n',[Suite,Name])
        ;   pj_inc_fail, format('  FAIL: ~w:~w  (check failed: ~w)~n',[Suite,Name,Expr])
        )
    ;   pj_inc_fail, format('  FAIL: ~w:~w  (goal failed)~n',[Suite,Name])
    ).

run_all(Suite, Name, Goal, (Var == Expected)) :-
    findall(Var, Goal, Actual),
    ( Actual == Expected ->
        pj_inc_pass, format('  pass: ~w:~w~n',[Suite,Name])
    ;   pj_inc_fail, format('  FAIL: ~w:~w  (all mismatch)~n',[Suite,Name])
    ).
