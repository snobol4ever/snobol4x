%-------------------------------------------------------------------------------
% 18
% In Luncyville the shoe store is closed every Monday, the hardware store every
% Tuesday, the grocery every Thursday, and the bank is open only Monday,
% Wednesday, and Friday. Everything is closed Sunday.
% Mrs. Abbott and Mrs. Denny: no day earlier in the week when both could go.
% Mrs. Briggs: didn't want today, but tomorrow she couldn't do her errand.
% Mrs. Culver: could have gone yesterday or the day before just as well.
% Mrs. Denny: either yesterday or tomorrow would have suited her.
% Which place did each woman need to visit?
%-------------------------------------------------------------------------------
:- initialization(main). main :- puzzle; true.

member(X, [X|_]).
member(X, [_|T]) :- member(X, T).
differ(X, X) :- !, fail.
differ(_, _).

% open(Store, Day) — true if store is open that day
open(shoe,     tuesday).   open(shoe,     wednesday). open(shoe,     thursday).
open(shoe,     friday).    open(shoe,     saturday).
open(hardware, monday).    open(hardware, wednesday). open(hardware, thursday).
open(hardware, friday).    open(hardware, saturday).
open(grocery,  monday).    open(grocery,  tuesday).   open(grocery,  wednesday).
open(grocery,  friday).    open(grocery,  saturday).
open(bank,     monday).    open(bank,     wednesday). open(bank,     friday).

prev_day(tuesday,   monday).    next_day(monday,    tuesday).
prev_day(wednesday, tuesday).   next_day(tuesday,   wednesday).
prev_day(thursday,  wednesday). next_day(wednesday, thursday).
prev_day(friday,    thursday).  next_day(thursday,  friday).
prev_day(saturday,  friday).    next_day(friday,    saturday).

day_num(monday,1). day_num(tuesday,2). day_num(wednesday,3).
day_num(thursday,4). day_num(friday,5). day_num(saturday,6).

store(S) :- member(S, [shoe, bank, grocery, hardware]).

puzzle :-
    member(Today, [monday,tuesday,wednesday,thursday,friday,saturday]),
    store(SAb), store(SBr), store(SCu), store(SDe),
    differ(SAb,SBr), differ(SAb,SCu), differ(SAb,SDe),
    differ(SBr,SCu), differ(SBr,SDe), differ(SCu,SDe),
    % All stores open today
    open(SAb,Today), open(SBr,Today), open(SCu,Today), open(SDe,Today),
    % Briggs: can't go tomorrow
    next_day(Today, Tomorrow),
    \+ open(SBr, Tomorrow),
    % Culver: could have gone yesterday or day before
    prev_day(Today, Yesterday),
    open(SCu, Yesterday),
    prev_day(Yesterday, DayBefore),
    open(SCu, DayBefore),
    % Denny: yesterday or tomorrow would suit
    open(SDe, Yesterday),
    open(SDe, Tomorrow),
    % Abbott+Denny: no earlier day when BOTH could go
    \+ (member(D,[monday,tuesday,wednesday,thursday,friday,saturday]),
        day_num(D,N), day_num(Today,NT), N < NT,
        open(SAb,D), open(SDe,D)),
    display(Today, SAb, SBr, SCu, SDe),
    fail.

display(Today, SAb, SBr, SCu, SDe) :-
    write('Day='),     write(Today),
    write(' Abbott='), write(SAb),
    write(' Briggs='), write(SBr),
    write(' Culver='), write(SCu),
    write(' Denny='),  write(SDe),
    write('\n').
