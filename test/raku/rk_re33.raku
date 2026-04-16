# rk_re33.raku — RK-33: NFA simulation / matching gate

sub main() {
    # \d+ — digit sequence
    if ('abc123' ~~ /\d+/) { say('digit match ok'); } else { say('FAIL digit'); }
    if ('abc' ~~ /\d+/)    { say('FAIL no digit'); } else { say('no digit ok'); }

    # [a-z]+ — lowercase word
    if ('hello' ~~ /[a-z]+/) { say('lower match ok'); } else { say('FAIL lower'); }
    if ('123' ~~ /[a-z]+/)   { say('FAIL no lower'); } else { say('no lower ok'); }

    # a|b — alternation
    if ('cat' ~~ /a|b/) { say('alt a ok'); } else { say('FAIL alt a'); }
    if ('dog' ~~ /a|b/) { say('FAIL alt b'); } else { say('no alt ok'); }

    # .* — any (unanchored, always matches empty)
    if ('' ~~ /.*/)    { say('dotstar empty ok'); } else { say('FAIL dotstar'); }
    if ('xyz' ~~ /.*/) { say('dotstar str ok');   } else { say('FAIL dotstar str'); }

    # ^x$ — anchored exact match
    if ('x' ~~ /^x$/)  { say('anchor ok'); }    else { say('FAIL anchor'); }
    if ('xy' ~~ /^x$/) { say('FAIL anchor long'); } else { say('anchor long ok'); }
    if ('' ~~ /^x$/)   { say('FAIL anchor empty'); } else { say('anchor empty ok'); }

    say('rk_re33 ok');
}
