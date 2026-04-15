// wordcount.sc — word counting (SC-15)
// Splits on spaces using SUBSTR, counts words into a TABLE

procedure SplitWords(text, words, i, sz, w, c) {
    words = TABLE();
    sz = SIZE(text);
    i = 1; w = '';
    while (LE(i, sz)) {
        c = SUBSTR(text, i, 1);
        if (IDENT(c, ' ')) {
            if (DIFFER(w, '')) { words[w] = words[w] + 1; w = ''; }
        } else {
            w = w && c;
        }
        i = i + 1;
    }
    if (DIFFER(w, '')) { words[w] = words[w] + 1; }
    SplitWords = words;
}

wc = SplitWords('the cat sat on the mat the cat');
OUTPUT = 'the=' && wc['the'];
OUTPUT = 'cat=' && wc['cat'];
OUTPUT = 'sat=' && wc['sat'];
OUTPUT = 'on='  && wc['on'];
OUTPUT = 'mat=' && wc['mat'];
