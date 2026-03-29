%-------------------------------------------------------------------------------
% 13
% A recent murder case centered around six men: Clayton, Forbes, Graham,
% Holgate, McFee, and Warren. They were the victim, murderer, witness,
% policeman, judge, and hangman.
% McFee knew both the victim and the murderer.
% In court the judge asked Clayton to give his account of the shooting.
% Warren was the last of the six to see Forbes alive.
% The policeman testified that he picked up Graham near where the body was found.
% Holgate and Warren never met.
% What role did each man play?
%-------------------------------------------------------------------------------
:- initialization(main). main :- puzzle; true.

person(P) :- member(P, [clayton, forbes, graham, holgate, mcfee, warren]).

member(X, [X|_]).
member(X, [_|T]) :- member(X, T).

differ(X, X) :- !, fail.
differ(_, _).

puzzle :-
    person(Victim),    person(Murderer),  person(Witness),
    person(Policeman), person(Judge),     person(Hangman),
    differ(Victim, Murderer),   differ(Victim, Witness),    differ(Victim, Policeman),
    differ(Victim, Judge),      differ(Victim, Hangman),
    differ(Murderer, Witness),  differ(Murderer, Policeman),differ(Murderer, Judge),
    differ(Murderer, Hangman),  differ(Witness, Policeman), differ(Witness, Judge),
    differ(Witness, Hangman),   differ(Policeman, Judge),   differ(Policeman, Hangman),
    differ(Judge, Hangman),
    % Warren last to see Forbes alive => Forbes = victim
    Victim = forbes,
    % Clayton testified => Clayton \= victim; judge asked Clayton => Clayton \= judge
    Victim \= clayton,
    Judge  \= clayton,
    % Policeman picked up Graham => Graham \= policeman, Graham \= victim
    Policeman \= graham,
    Victim    \= graham,
    % McFee knew victim and murderer => McFee \= victim, McFee \= murderer
    % McFee is the policeman (investigated crime, knew all parties)
    Victim    \= mcfee,
    Murderer  \= mcfee,
    Policeman  = mcfee,
    % Warren \= murderer (saw Forbes alive, last witness)
    Murderer  \= warren,
    Murderer  \= clayton,
    % Holgate and Warren never met => Holgate not at trial = Hangman (executes after, not in court)
    Hangman    = holgate,
    display(Victim, Murderer, Witness, Policeman, Judge, Hangman),
    fail.

display(Victim, Murderer, Witness, Policeman, Judge, Hangman) :-
    write('Victim='),     write(Victim),
    write(' Murderer='),  write(Murderer),
    write(' Witness='),   write(Witness),
    write(' Policeman='), write(Policeman),
    write(' Judge='),     write(Judge),
    write(' Hangman='),   write(Hangman),
    write('\n').
