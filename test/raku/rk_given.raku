# rk_given.raku — RK-13: given/when scalar smart-match
# given $x { when val { } ... default { } } lowers to nested E_IF chain.
# Numeric when: E_EQ comparison. String when: E_LEQ comparison.

sub day_type($d) {
    given $d {
        when 1 { say('Mon: weekday'); }
        when 2 { say('Tue: weekday'); }
        when 6 { say('Sat: weekend'); }
        when 7 { say('Sun: weekend'); }
        default { say('weekday');     }
    }
}

sub season($s) {
    given $s {
        when 'spring' { say('warm');  }
        when 'summer' { say('hot');   }
        when 'autumn' { say('cool');  }
        when 'winter' { say('cold');  }
        default       { say('unknown'); }
    }
}

sub main() {
    day_type(1);
    day_type(6);
    day_type(3);
    season('summer');
    season('winter');
    season('monsoon');
}
