# RK-15: Hash operations — set/get/exists/keys/values, sigil syntax

# Basic set and get via builtin calls
my $h = 0;
hash_set($h, 'name', 'Alice');
hash_set($h, 'age', '30');
hash_set($h, 'city', 'Portland');

say(hash_get($h, 'name'));
say(hash_get($h, 'age'));
say(hash_get($h, 'city'));

# exists
say(hash_exists($h, 'name'));
say(hash_exists($h, 'missing'));

# update existing key
hash_set($h, 'age', '31');
say(hash_get($h, 'age'));

# sigil syntax: %h<key> and %h{$k}
my %h2 = 0;
%h2<lang> = 'Raku';
%h2<vers> = '6';
say(%h2<lang>);
say(%h2<vers>);
my $k = 'lang';
say(%h2{$k});
