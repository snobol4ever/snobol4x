% family_prolog.pro — Prolog block of the Scrip family-tree demo.
% Uses only builtins confirmed supported by scrip-cc -pl -jvm parser+emitter.
% Deduplication of symmetric pairs done in Icon output stage.

% ── Pure helpers ──────────────────────────────────────────────────────────

findall_length([], 0).
findall_length([_|T], N) :- findall_length(T, N1), N is N1 + 1.

join_lines([], '').
join_lines([H|T], Result) :-
    join_lines(T, Rest),
    (Rest = '' -> Result = H ; atom_concat(H, '\n', HN), atom_concat(HN, Rest, Result)).

% ── Inference rules ───────────────────────────────────────────────────────

grandparent(GP, GC) :- parent(GC, P), parent(P, GP).

ancestor(A, D) :- parent(D, A).
ancestor(A, D) :- parent(D, P), ancestor(A, P).

sibling(X, Y) :- parent(X, P), parent(Y, P), X \= Y.

cousin(X, Y) :- parent(X, PX), parent(Y, PY), sibling(PX, PY), X \= Y.

generation(UID, 0) :- \+ parent(UID, _).
generation(UID, G) :- parent(UID, P), generation(P, PG), G is PG + 1.

% ── Line-builder rules (called from findall via named rule) ───────────────

gp_line(Line) :-
    grandparent(GP, GC),
    person(GPName, GP, _, _),
    person(GCName, GC, _, _),
    atom_concat(GPName, ' is grandparent of ', T),
    atom_concat(T, GCName, Line).

sib_line(Line) :-
    sibling(X, Y),
    person(XName, X, _, _),
    person(YName, Y, _, _),
    atom_concat(XName, '|', K1),
    atom_concat(K1, YName, Line).

cou_line(Line) :-
    cousin(X, Y),
    person(XName, X, _, _),
    person(YName, Y, _, _),
    atom_concat(XName, '|', K1),
    atom_concat(K1, YName, Line).

gen_line(Line) :-
    person(Name, UID, _, _),
    generation(UID, G),
    number_codes(G, GC),
    atom_codes(GA, GC),
    atom_concat(GA, '|', T),
    atom_concat(T, Name, Line).

anc_line(UID, Line) :-
    ancestor(A, UID),
    person(Name, A, _, _),
    generation(A, G),
    number_codes(G, GC),
    atom_codes(GA, GC),
    atom_concat(GA, '|', T),
    atom_concat(T, Name, Line).

% ── Cross-language entry points ───────────────────────────────────────────

scrip_init :- true.

assert_person(UID, Name, Year, Gender) :-
    assertz(person(Name, UID, Year, Gender)).

assert_parent(ChildUID, ParentUID) :-
    assertz(parent(ChildUID, ParentUID)).

query_count(Result) :-
    findall(x, person(_, _, _, _), L),
    findall_length(L, N),
    number_codes(N, NC),
    atom_codes(Result, NC).

query_grandparents(Result) :-
    findall(Line, gp_line(Line), Lines),
    join_lines(Lines, Result).

query_siblings(Result) :-
    findall(Line, sib_line(Line), Lines),
    join_lines(Lines, Result).

query_cousins(Result) :-
    findall(Line, cou_line(Line), Lines),
    join_lines(Lines, Result).

query_generations(Result) :-
    findall(Line, gen_line(Line), Lines),
    join_lines(Lines, Result).

query_ancestors(UID, Result) :-
    findall(Line, anc_line(UID, Line), Lines),
    join_lines(Lines, Result).

:- initialization(main).
main :- true.

% Cross-language entry points — exported via Byrd-box ABI
:- export(scrip_init/0).
:- export(assert_person/4).
:- export(assert_parent/2).
:- export(query_count/1).
:- export(query_grandparents/1).
:- export(query_siblings/1).
:- export(query_cousins/1).
:- export(query_generations/1).
:- export(query_ancestors/2).
