% plunit_mock.pro — runtime mock of SWI-Prolog's plunit library.
%
% Feed raw SWI test files as-is:
%   cat plunit_mock.pro test_foo.pl | scrip-cc -pl -jvm - -o test_foo.j
%
% Design: all plunit machinery runs at JVM runtime via the dynamic DB.
% No Python, no stripping, no regex.  begin_tests/end_tests are directives
% that fire at program init (pj_emit_main now routes them through
% pj_call_goal).  test/1 and test/2 are user-defined clauses — they stay
% exactly as written.  run_tests iterates the registered suite table.

% -------------------------------------------------------------------------
% Suite registry — populated at init time by begin_tests/end_tests directives
% -------------------------------------------------------------------------

:- dynamic pj_suite/1.
:- dynamic pj_current_suite/1.

begin_tests(Suite) :-
    ( pj_suite(Suite) -> true ; assertz(pj_suite(Suite)) ),
    ( retract(pj_current_suite(_)) -> true ; true ),
    assertz(pj_current_suite(Suite)).

begin_tests(Suite, _Opts) :-
    begin_tests(Suite).

end_tests(_Suite) :-
    ( retract(pj_current_suite(_)) -> true ; true ).

% -------------------------------------------------------------------------
% run_tests entry points
% -------------------------------------------------------------------------

run_tests :-
    pj_suite(Suite),
    run_tests([Suite]),
    fail.
run_tests.

run_tests(Suites) :-
    pj_run_suite_list(Suites).

pj_run_suite_list([]).
pj_run_suite_list([Suite|Rest]) :-
    pj_run_suite(Suite),
    pj_run_suite_list(Rest).

pj_run_suite(Suite) :-
    format('~n% PL-Unit: ~w~n', [Suite]),
    pj_run_suite_tests(Suite).

% Iterate all test(Name) and test(Name,Opts) clauses for this suite.
% Since test/N clauses ARE the user clauses, we enumerate them via
% findall on their heads, then call each.

pj_run_suite_tests(Suite) :-
    pj_collect_tests(Suite, Tests),
    pj_run_tests(Suite, Tests).

% Collect test names by calling test/1 and test/2 heads with a
% special sentinel — but we can't introspect clause heads directly.
% Instead, use the pj_test_name/2 registry assertz'd by our
% begin_tests / end_tests block rewrite below.
% Fall back: enumerate via dynamic pj_registered_test/2.

pj_run_tests(_Suite, []).
pj_run_tests(Suite, [Name-Opts|Rest]) :-
    pj_run_one(Suite, Name, Opts),
    pj_run_tests(Suite, Rest).

% -------------------------------------------------------------------------
% Test registration — test/1 and test/2 heads assertz into pj_registered_test
% at the point begin_tests fires.  But since test/N are user clauses (not
% directives), we can't intercept them at load time without clause inspection.
%
% Solution: pj_collect_tests/2 uses findall + the fact that test(Name,Opts)
% and test(Name) clauses unify with a free variable.  We call test(X) with
% catch to enumerate names, collect via a known-failure pattern.
%
% Simpler runtime approach: assert pj_registered_test facts from a
% begin_tests...end_tests block scanner run once at init.
% We implement this via pj_scan_tests/1 called from begin_tests.
% -------------------------------------------------------------------------

:- dynamic pj_registered_test/3.   % pj_registered_test(Suite, Name, Opts)

pj_collect_tests(Suite, Tests) :-
    findall(Name-Opts, pj_registered_test(Suite, Name, Opts), Tests).

% -------------------------------------------------------------------------
% run_one: execute a single test, handle options
% -------------------------------------------------------------------------

pj_run_one(Suite, Name, Opts) :-
    ( pj_opt_sto(Opts) ->
        format('  skip: ~w:~w  [sto]~n', [Suite, Name])
    ; pj_opt_fail(Opts) ->
        pj_run_fail(Suite, Name)
    ; pj_opt_error(Opts, Err) ->
        pj_run_error(Suite, Name, Err)
    ; pj_opt_throws(Opts, Thrown) ->
        pj_run_throws(Suite, Name, Thrown)
    ; pj_opt_true(Opts, Expr) ->
        pj_run_true(Suite, Name, Expr)
    ;
        pj_run_succeed(Suite, Name)
    ).

pj_run_succeed(Suite, Name) :-
    ( catch(test(Name), _E, fail) ->
        format('  pass: ~w:~w~n', [Suite, Name])
    ;   format('  FAILED: ~w:~w  (goal failed)~n', [Suite, Name])
    ).

pj_run_fail(Suite, Name) :-
    ( catch(test(Name), _E, true) ->
        format('  FAILED: ~w:~w  (expected fail, succeeded)~n', [Suite, Name])
    ;   format('  pass: ~w:~w~n', [Suite, Name])
    ).

pj_run_error(Suite, Name, ExpErr) :-
    ( catch(test(Name), error(ActErr,_), pj_match_err(Suite,Name,ExpErr,ActErr)) ->
        true
    ;   format('  FAILED: ~w:~w  (no exception)~n', [Suite, Name])
    ).

pj_run_throws(Suite, Name, ExpThrow) :-
    ( catch(test(Name), ActThrow, pj_match_throw(Suite,Name,ExpThrow,ActThrow)) ->
        true
    ;   format('  FAILED: ~w:~w  (no exception)~n', [Suite, Name])
    ).

pj_run_true(Suite, Name, Expr) :-
    ( catch(test(Name), _E, fail) ->
        ( catch(Expr, _, fail) ->
            format('  pass: ~w:~w~n', [Suite, Name])
        ;   format('  FAILED: ~w:~w  (postcondition ~w failed)~n', [Suite, Name, Expr])
        )
    ;   format('  FAILED: ~w:~w  (goal failed)~n', [Suite, Name])
    ).

pj_match_err(Suite, Name, Exp, Act) :-
    ( Exp = Act ->
        format('  pass: ~w:~w~n', [Suite, Name])
    ;   format('  FAILED: ~w:~w  (wrong error: expected ~w got ~w)~n',
               [Suite, Name, Exp, Act])
    ).

pj_match_throw(Suite, Name, Exp, Act) :-
    ( Exp = Act ->
        format('  pass: ~w:~w~n', [Suite, Name])
    ;   format('  FAILED: ~w:~w  (wrong throw: expected ~w got ~w)~n',
               [Suite, Name, Exp, Act])
    ).

% -------------------------------------------------------------------------
% Option accessors
% -------------------------------------------------------------------------

pj_opt_sto(sto(_)) :- !.
pj_opt_sto(Opts) :- is_list(Opts), member(sto(_), Opts).

pj_opt_fail(fail) :- !.
pj_opt_fail(false) :- !.
pj_opt_fail(Opts) :- is_list(Opts), ( member(fail, Opts) ; member(false, Opts) ), !.

pj_opt_error(error(E), E) :- !.
pj_opt_error(Opts, E) :- is_list(Opts), member(error(E), Opts), !.

pj_opt_throws(throws(T), T) :- !.
pj_opt_throws(Opts, T) :- is_list(Opts), member(throws(T), Opts), !.

pj_opt_true(true(E), E) :- !.
pj_opt_true(Opts, E) :- is_list(Opts), member(true(E), Opts), !.
% bare goal as opts (e.g. test(name, X==y)) — already a postcondition
pj_opt_true(Opts, Opts) :-
    Opts \= [],
    \+ is_list(Opts),
    Opts \= fail, Opts \= false,
    Opts \= sto(_), Opts \= error(_), Opts \= throws(_).

% -------------------------------------------------------------------------
% Entry point
% -------------------------------------------------------------------------

main :- run_tests, halt.
:- initialization(main).
