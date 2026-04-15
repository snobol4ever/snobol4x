# rk_unless_until.raku — RK-20: unless, until
# repeat deferred to when 'last' is available

# unless: body runs when condition is false
my Int $x = 5;
unless ($x == 10) {
    say "not ten";
}

# unless with else: else branch taken when condition is true
unless ($x == 5) {
    say "wrong";
} else {
    say "five";
}

# until: loops while condition is false, stops when true
my Int $i = 0;
until ($i >= 3) {
    say $i;
    $i = $i + 1;
}

say "done";
