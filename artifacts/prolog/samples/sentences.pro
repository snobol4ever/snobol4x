% sentences.pro — Tiny English parser via DCG
% Prolog idiom: Definite Clause Grammars — rules ARE the parser
% Shows: DCG notation, phrase/2, both parsing and generation
:- initialization(main, main).

sentence --> noun_phrase(N), verb_phrase(N).

noun_phrase(N) --> det(N), noun(N).
noun_phrase(N) --> noun(N).

verb_phrase(N) --> verb(N, intrans).
verb_phrase(N) --> verb(N, trans), noun_phrase(_).

det(sg) --> [the].
det(sg) --> [a].
det(pl) --> [the].

noun(sg) --> [cat].
noun(sg) --> [dog].
noun(sg) --> [mouse].
noun(pl) --> [cats].
noun(pl) --> [dogs].
noun(pl) --> [mice].

verb(sg, intrans) --> [sleeps].
verb(sg, intrans) --> [runs].
verb(pl, intrans) --> [sleep].
verb(pl, intrans) --> [run].
verb(sg, trans)   --> [chases].
verb(pl, trans)   --> [chase].

main :-
    Tests = [
        [the, cat, sleeps],
        [a, dog, chases, the, mouse],
        [cats, chase, mice],
        [the, mouse, runs],
        [cats, chases, mice]      % bad agreement — should fail
    ],
    forall(member(S, Tests), (
        ( phrase(sentence, S) ->
            format("  PASS  ~w~n", [S])
        ;
            format("  FAIL  ~w~n", [S])
        )
    )),
    nl,
    write("Generated sentences:"), nl,
    findall(S, (phrase(sentence, S), length(S, 3)), Ss),
    forall(member(S, Ss), (write("  "), write(S), nl)).
