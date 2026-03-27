:- export(ancestor/2).

parent(tom, bob).
parent(bob, ann).
ancestor(X,Y) :- parent(X,Y).
ancestor(X,Y) :- parent(X,Z), ancestor(Z,Y).
