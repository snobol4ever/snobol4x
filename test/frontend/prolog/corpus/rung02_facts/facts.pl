% rung02_facts — deterministic fact lookup
% Expected output (one per line): brown jones smith
:- initialization(main).
person(brown).
person(jones).
person(smith).
main :- person(X), write(X), nl, fail ; true.
