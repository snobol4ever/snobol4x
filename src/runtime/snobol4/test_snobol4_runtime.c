/* test_snobol4_runtime.c — smoke test for Sprint 20 snobol4.c runtime
 *
 * Build: cc -o test_runtime test_snobol4_runtime.c snobol4.c -I. -lgc -lm
 * Run:   ./test_runtime
 */

#include "snobol4.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static int passed = 0, failed = 0;

#define CHECK(desc, cond) do { \
    if (cond) { printf("  PASS: %s\n", desc); passed++; } \
    else       { printf("  FAIL: %s\n", desc); failed++; } \
} while(0)

#define STREQ(a,b) (strcmp((a),(b))==0)

int main(void) {
    runtime_init();
    printf("snobol4.c runtime smoke test\n");
    printf("============================================================\n");

    /* --- SnoVal conversions --- */
    printf("\n[SnoVal conversions]\n");
    CHECK("int to strv",       STREQ(to_str(INT_VAL(42)), "42"));
    CHECK("int negative",     STREQ(to_str(INT_VAL(-7)), "-7"));
    CHECK("strv to int",       to_int(STR_VAL("123")) == 123);
    CHECK("null to strv empty",STREQ(to_str(NULL_VAL), ""));
    CHECK("null to int 0",    to_int(NULL_VAL) == 0);
    CHECK("datatype INT",     STREQ(datatype(INT_VAL(1)), "INTEGER"));
    CHECK("datatype STR",     STREQ(datatype(STR_VAL("x")), "STRING"));
    CHECK("datatype NULL",    STREQ(datatype(NULL_VAL), "STRING"));

    /* --- String ops --- */
    printf("\n[String operations]\n");
    CHECK("ccat",    STREQ(ccat("hello", " world"), "hello world"));
    CHECK("ccat empty", STREQ(ccat("", "x"), "x"));
    CHECK("size",      size("hello") == 5);
    CHECK("size empty",size("") == 0);
    CHECK("size null", size(NULL) == 0);

    /* --- Builtin functions --- */
    printf("\n[Builtin functions]\n");
    SnoVal sz = size_fn(STR_VAL("hello"));
    CHECK("SIZE('hello')=5", sz.type==SINT && sz.i==5);

    SnoVal dupl = dupl_fn(STR_VAL("ab"), INT_VAL(3));
    CHECK("DUPL('ab',3)='ababab'", STREQ(to_str(dupl), "ababab"));

    SnoVal trim = trim_fn(STR_VAL("hello   "));
    CHECK("TRIM('hello   ')='hello'", STREQ(to_str(trim), "hello"));

    SnoVal lpad = lpad_fn(STR_VAL("hi"), INT_VAL(5), STR_VAL(" "));
    CHECK("LPAD('hi',5)='   hi'", STREQ(to_str(lpad), "   hi"));

    SnoVal rpad = rpad_fn(STR_VAL("hi"), INT_VAL(5), STR_VAL(" "));
    CHECK("RPAD('hi',5)='hi   '", STREQ(to_str(rpad), "hi   "));

    SnoVal sub = substr_fn(STR_VAL("hello"), INT_VAL(2), INT_VAL(3));
    CHECK("SUBSTR('hello',2,3)='ell'", STREQ(to_str(sub), "ell"));

    SnoVal ch = char_fn(INT_VAL(65));
    CHECK("CHAR(65)='A'", STREQ(to_str(ch), "A"));

    SnoVal iv = integer_fn(STR_VAL("42"));
    CHECK("INTEGER('42')=42", iv.type==SINT && iv.i==42);

    SnoVal rv = real_fn(INT_VAL(3));
    CHECK("REAL(3)=3.0", rv.type==SREAL && rv.r==3.0);

    /* REPLACE: tr-style */
    SnoVal rep = replace_fn(STR_VAL("hello"),
                                STR_VAL("aeiou"),
                                STR_VAL("AEIOU"));
    CHECK("REPLACE('hello',vowels,VOWELS)='hEllO'", STREQ(to_str(rep), "hEllO"));

    /* --- Arithmetic --- */
    printf("\n[Arithmetic]\n");
    SnoVal a = INT_VAL(10), b = INT_VAL(3);
    CHECK("10+3=13",  add(a,b).i == 13);
    CHECK("10-3=7",   sub(a,b).i == 7);
    CHECK("10*3=30",  mul(a,b).i == 30);
    CHECK("10/3=3",   dyvide(a,b).i == 3);   /* integer division */
    CHECK("10 EQ 10", eq(a, INT_VAL(10)));
    CHECK("10 NE 3",  ne(a, b));
    CHECK("3 LT 10",  lt(b, a));
    CHECK("10 GT 3",  gt(a, b));
    CHECK("10 GE 10", ge(a, INT_VAL(10)));
    CHECK("3 LE 10",  le(b, a));

    /* --- IDENT / DIFFER --- */
    printf("\n[IDENT / DIFFER]\n");
    CHECK("IDENT('x','x')=1", ident(STR_VAL("x"), STR_VAL("x")));
    CHECK("IDENT('x','y')=0", !ident(STR_VAL("x"), STR_VAL("y")));
    CHECK("IDENT(1,1)=1",     ident(INT_VAL(1), INT_VAL(1)));
    CHECK("DIFFER('x','y')=1",differ(STR_VAL("x"), STR_VAL("y")));
    /* null == empty string */
    CHECK("IDENT(null,'')=1", ident(NULL_VAL, STR_VAL("")));

    /* --- Variable table --- */
    printf("\n[Variable table]\n");
    var_set("FOO", INT_VAL(99));
    CHECK("var set/get", var_get("FOO").i == 99);
    var_set("BAR", STR_VAL("hello"));
    CHECK("var strv", STREQ(to_str(var_get("BAR")), "hello"));
    SnoVal miss = var_get("MISSING");
    CHECK("missing var = null", miss.type == SNULL);

    /* $name indirect: FOO contains "BAR", $FOO should give BAR's value */
    var_set("PTR", STR_VAL("BAR"));
    SnoVal indirect = indirect_get("PTR");
    CHECK("indirect $PTR=BAR='hello'", STREQ(to_str(indirect), "hello"));

    /* --- Counter stack --- */
    printf("\n[Counter stack]\n");
    npush();
    CHECK("nTop after push=0",  ntop() == 0);
    ninc();
    ninc();
    ninc();
    CHECK("nTop after 3 inc=3", ntop() == 3);
    ndec();
    CHECK("nTop after dec=2",   ntop() == 2);
    npop();
    /* stack should be empty now — ntop returns 0 */
    npush();
    CHECK("fresh push=0",       ntop() == 0);
    npop();

    /* --- Value stack --- */
    printf("\n[Value stack]\n");
    push(INT_VAL(1));
    push(INT_VAL(2));
    push(INT_VAL(3));
    CHECK("stack depth=3",   stack_depth() == 3);
    CHECK("top=3",           top().i == 3);
    CHECK("pop=3",           pop().i == 3);
    CHECK("pop=2",           pop().i == 2);
    CHECK("stack depth=1",   stack_depth() == 1);
    pop();
    CHECK("stack empty=0",   stack_depth() == 0);

    /* --- Tree --- */
    printf("\n[Tree]\n");
    Tree *root = tree_new0("expr");
    Tree *c1   = tree_new("num", INT_VAL(5));
    Tree *c2   = tree_new("num", INT_VAL(7));
    tree_append(root, c1);
    tree_append(root, c2);
    CHECK("tree tag",      STREQ(t(root), "expr"));
    CHECK("tree n=2",      n(root) == 2);
    CHECK("tree c[1]=5",   c_i(root, 1)->val.i == 5);
    CHECK("tree c[2]=7",   c_i(root, 2)->val.i == 7);
    tree_prepend(root, tree_new0("dummy"));
    CHECK("prepend n=3",   n(root) == 3);
    CHECK("prepend c[1]=dummy", STREQ(c_i(root,1)->tag, "dummy"));
    Tree *removed = tree_remove(root, 1);
    CHECK("remove dummy",  STREQ(removed->tag, "dummy"));
    CHECK("after remove n=2", n(root) == 2);

    /* --- Array --- */
    printf("\n[Array]\n");
    SnoArray *arr = array_new(1, 4);
    array_set(arr, 1, INT_VAL(10));
    array_set(arr, 4, INT_VAL(40));
    CHECK("arr[1]=10", array_get(arr, 1).i == 10);
    CHECK("arr[4]=40", array_get(arr, 4).i == 40);
    CHECK("arr[2]=null", array_get(arr, 2).type == SNULL);
    CHECK("arr OOB=null", array_get(arr, 99).type == SNULL);

    /* --- Table --- */
    printf("\n[Table]\n");
    SnoTable *tbl = table_new();
    table_set(tbl, "key1", INT_VAL(100));
    table_set(tbl, "key2", STR_VAL("value"));
    CHECK("tbl key1=100", table_get(tbl, "key1").i == 100);
    CHECK("tbl key2=value", STREQ(to_str(table_get(tbl, "key2")), "value"));
    CHECK("tbl has key1",  table_has(tbl, "key1"));
    CHECK("tbl no key3",  !table_has(tbl, "key3"));
    table_set(tbl, "key1", INT_VAL(999));
    CHECK("tbl update",   table_get(tbl, "key1").i == 999);

    /* --- DATA() / UDef --- */
    printf("\n[DATA / UDef]\n");
    data_define("node(left,right,value)");
    SnoVal n = udef_new("node",
                            NULL_VAL,  /* left  */
                            NULL_VAL,  /* right */
                            INT_VAL(42), /* value */
                            (SnoVal){0}    /* sentinel */);
    CHECK("udef type=node", STREQ(datatype(n), "node"));
    SnoVal val = field_get(n, "value");
    CHECK("udef field value=42", val.i == 42);
    field_set(n, "value", INT_VAL(99));
    CHECK("udef field set", field_get(n, "value").i == 99);

    /* --- Function table --- */
    printf("\n[Function table]\n");
    /* Define a trivial add function */
    SnoFunc add_fn = NULL;
    (void)add_fn;
    define("double(x)", NULL);  /* no C body — just register */
    CHECK("func exists", func_exists("double"));
    CHECK("func not exists", !func_exists("nothing"));

    /* --- Summary --- */
    printf("\n============================================================\n");
    printf("Results: %d passed, %d failed\n", passed, failed);
    if (failed == 0)
        printf("ALL PASS — snobol4.c runtime is correct.\n");
    return failed ? 1 : 0;
}
