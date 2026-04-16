# rk_re32.raku — RK-32: NFA compilation gate
# Verifies NFA builds for target patterns and reports state counts.
# Execution (matching) is tested in RK-33.

sub main() {
    # The NFA state count is printed by the raku_nfa_compile builtin.
    # Each call prints:  NFA:<pattern>:states=N
    raku_nfa_compile('\d+');
    raku_nfa_compile('[a-z]+');
    raku_nfa_compile('a|b');
    raku_nfa_compile('.*');
    raku_nfa_compile('^x$');
    say('rk_re32 ok');
}
