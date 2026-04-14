# rk_for_array.raku — RK-16: for @arr -> $x real array iteration
sub main() {
    my @nums = '';
    push(@nums, 10);
    push(@nums, 20);
    push(@nums, 30);

    for @nums -> $x {
        say($x);
    }

    my $sum = 0;
    for @nums -> $n {
        $sum = $sum + $n;
    }
    say($sum);

    my @words = '';
    push(@words, 'alpha');
    push(@words, 'beta');
    push(@words, 'gamma');
    for @words -> $w {
        say($w);
    }
}
