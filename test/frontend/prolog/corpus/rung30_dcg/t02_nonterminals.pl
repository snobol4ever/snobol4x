:- initialization(main).

sentence --> noun_phrase, verb_phrase.
noun_phrase --> [the], noun.
verb_phrase --> verb, noun_phrase.
verb_phrase --> verb.
noun --> [cat].
noun --> [dog].
noun --> [mouse].
verb --> [chases].
verb --> [sees].

main :-
    ( phrase(sentence, [the, cat, chases, the, mouse]) -> write(yes) ; write(no) ), nl,
    ( phrase(sentence, [the, dog, sees]) -> write(yes) ; write(no) ), nl,
    ( phrase(sentence, [cat, chases]) -> write(yes) ; write(no) ), nl.
