# rk_typed_vars.raku — RK-19: typed variable declarations
# Type annotations are parsed and silently discarded (no enforcement yet).

my Int $x = 42;
my Str $s = "hello";
my Num $f = 3;
my Bool $b = 1;

say $x;
say $s;
say $f;
say $b;

# Typed vars participate normally in expressions
my Int $y = $x + 8;
say $y;

my Str $t = $s ~ " world";
say $t;

# Uninitialized typed var (should work, binds to empty string)
my Int $z;
say "ok";
