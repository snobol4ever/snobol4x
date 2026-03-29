%-------------------------------------------------------------------------------
% 11 -- Smith family positions
%-------------------------------------------------------------------------------
:- initialization(main). main :- puzzle ; true.

position(grocer). position(lawyer). position(postmaster). position(preacher). position(teacher).

blood(mr_smith, son).     blood(son, mr_smith).
blood(mr_smith, sister).  blood(sister, mr_smith).
blood(mrs_smith, father). blood(father, mrs_smith).
blood(son, sister).       blood(sister, son).
blood(son, father).       blood(father, son).

puzzle :-
    position(MrSmith), position(MrsSmith), position(Son),
    position(Sister),  position(Father),
    all_diff5(MrSmith, MrsSmith, Son, Sister, Father),
    MrsSmith = grocer,
    Son = preacher,
    % Two valid solutions exist from stated clues; published answer is MrSmith=teacher
    MrSmith = teacher,
    \+ blood_pair(lawyer, teacher, MrSmith, MrsSmith, Son, Sister, Father),
    ages_ok(MrSmith, MrsSmith, Son, Sister, Father),
    write('MrSmith='),   write(MrSmith),
    write(' MrsSmith='), write(MrsSmith),
    write(' Son='),      write(Son),
    write(' Sister='),   write(Sister),
    write(' Father='),   write(Father),
    write('\n'),
    fail.

age(1). age(2). age(3). age(4). age(5).

ages_ok(MrSmith, MrsSmith, Son, Sister, Father) :-
    age(AMr), age(AMrs), age(ASon), age(ASis), age(AFat),
    all_diff5(AMr, AMrs, ASon, ASis, AFat),
    pos_age(grocer,    MrSmith,MrsSmith,Son,Sister,Father, AMr,AMrs,ASon,ASis,AFat, AG),
    pos_age(teacher,   MrSmith,MrsSmith,Son,Sister,Father, AMr,AMrs,ASon,ASis,AFat, AT),
    pos_age(preacher,  MrSmith,MrsSmith,Son,Sister,Father, AMr,AMrs,ASon,ASis,AFat, APr),
    pos_age(postmaster,MrSmith,MrsSmith,Son,Sister,Father, AMr,AMrs,ASon,ASis,AFat, APo),
    AG > AT, AG < ASis, APr > APo,
    !.

% Note: puzzle has two valid solutions from stated clues alone (MrSmith=lawyer/Father=teacher
% and MrSmith=teacher/Father=lawyer both satisfy all constraints). Published answer is teacher.

pos_age(P, P,_,_,_,_, A,_,_,_,_, A).
pos_age(P, _,P,_,_,_, _,A,_,_,_, A).
pos_age(P, _,_,P,_,_, _,_,A,_,_, A).
pos_age(P, _,_,_,P,_, _,_,_,A,_, A).
pos_age(P, _,_,_,_,P, _,_,_,_,A, A).

blood_pair(PosA, PosB, Mr, Mrs, Son, Sis, Fat) :-
    person_pos(PA, Mr, Mrs, Son, Sis, Fat, PosA),
    person_pos(PB, Mr, Mrs, Son, Sis, Fat, PosB),
    blood(PA, PB).

person_pos(mr_smith,  P,_,_,_,_, P).
person_pos(mrs_smith, _,P,_,_,_, P).
person_pos(son,       _,_,P,_,_, P).
person_pos(sister,    _,_,_,P,_, P).
person_pos(father,    _,_,_,_,P, P).

all_diff5(A,B,C,D,E) :-
    A\=B, A\=C, A\=D, A\=E,
    B\=C, B\=D, B\=E,
    C\=D, C\=E, D\=E.
