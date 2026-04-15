# rk_given18.raku — RK-18: given/when extended coverage
# Tests: nested given/when, given in a loop, default fallthrough,
# mixed numeric and string when-arms.

sub classify($n) {
    given $n {
        when 0 { say('zero');     }
        when 1 { say('one');      }
        when 2 { say('two');      }
        default { say('many');    }
    }
}

sub grade($s) {
    given $s {
        when 'A' { say('excellent'); }
        when 'B' { say('good');      }
        when 'C' { say('average');   }
        default  { say('other');     }
    }
}

sub nested($n) {
    given $n {
        when 1 {
            given 'A' {
                when 'A' { say('one-A'); }
                default  { say('one-?'); }
            }
        }
        default { say('not-one'); }
    }
}

sub in_loop() {
    my @vals = '';
    push(@vals, 1);
    push(@vals, 2);
    push(@vals, 3);
    push(@vals, 4);
    for @vals -> $v {
        given $v {
            when 1 { say('got-one');   }
            when 2 { say('got-two');   }
            default { say('got-other'); }
        }
    }
}

sub main() {
    classify(0);
    classify(1);
    classify(5);
    grade('A');
    grade('C');
    grade('F');
    nested(1);
    nested(2);
    in_loop();
}
