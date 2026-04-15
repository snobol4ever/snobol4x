// palindrome.sc — string reverse + palindrome check (SC-14)
procedure Reverse(s, r, c, i) {
    r = ''; i = SIZE(s);
    while (GT(i, 0)) { c = SUBSTR(s, i, 1); r = r && c; i = i - 1; }
    Reverse = r;
}
procedure IsPalindrome(s) {
    if (IDENT(s, Reverse(s))) { return; } else { freturn; }
}

OUTPUT = Reverse('hello');
OUTPUT = Reverse('abcba');
if (IsPalindrome('racecar'))  { OUTPUT = 'PASS: racecar'; }  else { OUTPUT = 'FAIL: racecar'; }
if (IsPalindrome('hello'))    { OUTPUT = 'FAIL: hello'; }    else { OUTPUT = 'PASS: hello not palindrome'; }
if (IsPalindrome('abcba'))    { OUTPUT = 'PASS: abcba'; }    else { OUTPUT = 'FAIL: abcba'; }
if (IsPalindrome('a'))        { OUTPUT = 'PASS: single'; }   else { OUTPUT = 'FAIL: single'; }
if (IsPalindrome(''))         { OUTPUT = 'PASS: empty'; }    else { OUTPUT = 'FAIL: empty'; }
