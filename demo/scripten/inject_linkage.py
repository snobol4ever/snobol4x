#!/usr/bin/env python3
"""inject_linkage.py -- Inject cross-language funny linkage into compiled .j files.

After sno2c -jvm and icon_driver -jvm produce their .j files, this script:
  1. Appends String-bridge public static methods to FamilyProlog.j
  2. Replaces SNOBOL4 stub method bodies in FamilySnobol4.j with invokestatic
     calls to FamilyProlog bridge methods
  3. Replaces Icon stub method bodies in FamilyIcon.j with invokestatic calls
     to FamilyProlog bridge methods

All cross-language currency is java/lang/String.
"""
import sys, re

def read(path):
    return open(path).read()

def write(path, text):
    open(path, 'w').write(text)
    print(f"  wrote {path} ({len(text.splitlines())} lines)")

# ---------------------------------------------------------------------------
# Step 1: append String-bridge methods to FamilyProlog.j
# ---------------------------------------------------------------------------
PROLOG_BRIDGES = r"""
; ===== Scripten funny-linkage bridges (injected by inject_linkage.py) =====

; scripten_init() -- called once by SNOBOL4 before CSV parsing.
.method public static scripten_init()V
    .limit stack 2
    .limit locals 1
    invokestatic Family_prolog/p_scripten_init_0(I)[Ljava/lang/Object;
    pop
    return
.end method

; assert_person(String uid, String name, String year, String gender)
.method public static assert_person(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V
    .limit stack 8
    .limit locals 5
    invokestatic Family_prolog/pj_term_var()[Ljava/lang/Object;
    astore 4
    invokestatic Family_prolog/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;
    astore 4
    aload_0
    invokestatic Family_prolog/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;
    aload_1
    invokestatic Family_prolog/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;
    aload_2
    invokestatic Family_prolog/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;
    aload_3
    invokestatic Family_prolog/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;
    iconst_0
    invokestatic Family_prolog/p_assert_person_4([Ljava/lang/Object;[Ljava/lang/Object;[Ljava/lang/Object;[Ljava/lang/Object;I)[Ljava/lang/Object;
    pop
    return
.end method

; assert_parent(String childUID, String parentUID)
.method public static assert_parent(Ljava/lang/String;Ljava/lang/String;)V
    .limit stack 4
    .limit locals 3
    aload_0
    invokestatic Family_prolog/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;
    aload_1
    invokestatic Family_prolog/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;
    iconst_0
    invokestatic Family_prolog/p_assert_parent_2([Ljava/lang/Object;[Ljava/lang/Object;I)[Ljava/lang/Object;
    pop
    return
.end method

; query_string_0(predName) helper -- call p_NAME_1(freshVar, 0) -> String
; Inline helpers per query (no generics in Jasmin).

.method public static query_count()Ljava/lang/String;
    .limit stack 4
    .limit locals 2
    invokestatic Family_prolog/pj_term_var()[Ljava/lang/Object;
    astore_0
    aload_0
    iconst_0
    invokestatic Family_prolog/p_query_count_1([Ljava/lang/Object;I)[Ljava/lang/Object;
    astore_1
    aload_1
    ifnull qcount_fail
    aload_0
    invokestatic Family_prolog/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;
    invokestatic Family_prolog/pj_term_str(Ljava/lang/Object;)Ljava/lang/String;
    areturn
qcount_fail:
    ldc "0"
    areturn
.end method

.method public static query_grandparents()Ljava/lang/String;
    .limit stack 4
    .limit locals 2
    invokestatic Family_prolog/pj_term_var()[Ljava/lang/Object;
    astore_0
    aload_0
    iconst_0
    invokestatic Family_prolog/p_query_grandparents_1([Ljava/lang/Object;I)[Ljava/lang/Object;
    astore_1
    aload_1
    ifnull qgp_fail
    aload_0
    invokestatic Family_prolog/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;
    invokestatic Family_prolog/pj_term_str(Ljava/lang/Object;)Ljava/lang/String;
    areturn
qgp_fail:
    ldc ""
    areturn
.end method

.method public static query_siblings()Ljava/lang/String;
    .limit stack 4
    .limit locals 2
    invokestatic Family_prolog/pj_term_var()[Ljava/lang/Object;
    astore_0
    aload_0
    iconst_0
    invokestatic Family_prolog/p_query_siblings_1([Ljava/lang/Object;I)[Ljava/lang/Object;
    astore_1
    aload_1
    ifnull qsib_fail
    aload_0
    invokestatic Family_prolog/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;
    invokestatic Family_prolog/pj_term_str(Ljava/lang/Object;)Ljava/lang/String;
    areturn
qsib_fail:
    ldc ""
    areturn
.end method

.method public static query_cousins()Ljava/lang/String;
    .limit stack 4
    .limit locals 2
    invokestatic Family_prolog/pj_term_var()[Ljava/lang/Object;
    astore_0
    aload_0
    iconst_0
    invokestatic Family_prolog/p_query_cousins_1([Ljava/lang/Object;I)[Ljava/lang/Object;
    astore_1
    aload_1
    ifnull qcou_fail
    aload_0
    invokestatic Family_prolog/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;
    invokestatic Family_prolog/pj_term_str(Ljava/lang/Object;)Ljava/lang/String;
    areturn
qcou_fail:
    ldc ""
    areturn
.end method

.method public static query_generations()Ljava/lang/String;
    .limit stack 4
    .limit locals 2
    invokestatic Family_prolog/pj_term_var()[Ljava/lang/Object;
    astore_0
    aload_0
    iconst_0
    invokestatic Family_prolog/p_query_generations_1([Ljava/lang/Object;I)[Ljava/lang/Object;
    astore_1
    aload_1
    ifnull qgen_fail
    aload_0
    invokestatic Family_prolog/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;
    invokestatic Family_prolog/pj_term_str(Ljava/lang/Object;)Ljava/lang/String;
    areturn
qgen_fail:
    ldc ""
    areturn
.end method

.method public static query_ancestors(Ljava/lang/String;)Ljava/lang/String;
    .limit stack 4
    .limit locals 3
    invokestatic Family_prolog/pj_term_var()[Ljava/lang/Object;
    astore_1
    aload_0
    invokestatic Family_prolog/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;
    aload_1
    iconst_0
    invokestatic Family_prolog/p_query_ancestors_2([Ljava/lang/Object;[Ljava/lang/Object;I)[Ljava/lang/Object;
    astore_2
    aload_2
    ifnull qanc_fail
    aload_1
    invokestatic Family_prolog/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;
    invokestatic Family_prolog/pj_term_str(Ljava/lang/Object;)Ljava/lang/String;
    areturn
qanc_fail:
    ldc ""
    areturn
.end method
"""

def inject_prolog(path):
    text = read(path)
    # Append bridges before the final blank line / EOF
    text = text.rstrip() + "\n" + PROLOG_BRIDGES + "\n"
    write(path, text)

# ---------------------------------------------------------------------------
# Step 2: patch SNOBOL4 stub method bodies
# ---------------------------------------------------------------------------

SNO_SCRIPTEN_INIT_BODY = """\
    .limit stack 2
    .limit locals 1
    invokestatic Family_prolog/scripten_init()V
    ldc ""
    areturn"""

SNO_ASSERT_PERSON_BODY = """\
    .limit stack 8
    .limit locals 5
    aload_0
    aload_1
    aload_2
    aload_3
    invokestatic Family_prolog/assert_person(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V
    ldc ""
    areturn"""

SNO_ASSERT_PARENT_BODY = """\
    .limit stack 4
    .limit locals 3
    aload_0
    aload_1
    invokestatic Family_prolog/assert_parent(Ljava/lang/String;Ljava/lang/String;)V
    ldc ""
    areturn"""

def replace_method_body(text, sig, new_body):
    """Replace everything between .method sig and .end method with new_body."""
    pat = re.compile(
        r'(' + re.escape(sig) + r'\n).*?(\n\.end method)',
        re.DOTALL)
    def repl(m):
        return m.group(1) + new_body + m.group(2)
    result, n = pat.subn(repl, text)
    if n == 0:
        print(f"  WARNING: method not found: {sig}")
    return result

def inject_snobol4(path):
    text = read(path)
    text = replace_method_body(text,
        ".method static sno_userfn_SCRIPTEN_INIT()Ljava/lang/String;",
        SNO_SCRIPTEN_INIT_BODY)
    text = replace_method_body(text,
        ".method static sno_userfn_PROLOG_ASSERT_PERSON(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        SNO_ASSERT_PERSON_BODY)
    text = replace_method_body(text,
        ".method static sno_userfn_PROLOG_ASSERT_PARENT(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        SNO_ASSERT_PARENT_BODY)
    write(path, text)

# ---------------------------------------------------------------------------
# Step 3: patch Icon stub method bodies
# ---------------------------------------------------------------------------

def icon_stub(method_sig, prolog_call):
    """Return a patched Icon stub body that calls FamilyProlog and stores
    result in icn_retval (String field) then returns."""
    return f"""\
    .limit stack 4
    .limit locals 2
    {prolog_call}
    putstatic Family_icon/icn_retval_str Ljava/lang/String;
    return"""

ICN_STUBS = {
    ".method public static icn_prolog_query_count()V": (
        "icn_retval",
        "invokestatic Family_prolog/query_count()Ljava/lang/String;"),
    ".method public static icn_prolog_query_grandparents()V": (
        "icn_retval",
        "invokestatic Family_prolog/query_grandparents()Ljava/lang/String;"),
    ".method public static icn_prolog_query_siblings()V": (
        "icn_retval",
        "invokestatic Family_prolog/query_siblings()Ljava/lang/String;"),
    ".method public static icn_prolog_query_cousins()V": (
        "icn_retval",
        "invokestatic Family_prolog/query_cousins()Ljava/lang/String;"),
    ".method public static icn_prolog_query_generations()V": (
        "icn_retval",
        "invokestatic Family_prolog/query_generations()Ljava/lang/String;"),
    ".method public static icn_prolog_query_ancestors()V": (
        "icn_retval_str",
        'ldc "U008"\n    invokestatic Family_prolog/query_ancestors(Ljava/lang/String;)Ljava/lang/String;'),
}

def inject_icon(path):
    text = read(path)
    # First: find the actual icn_retval field name (String type)
    # Icon emitter stores string retval in icn_retval_str or similar
    # Check what field the stub currently uses
    for sig, (field, call) in ICN_STUBS.items():
        body = f"""\
    .limit stack 4
    .limit locals 2
    {call}
    putstatic Family_icon/icn_retval_str Ljava/lang/String;
    return"""
        text = replace_method_body(text, sig, body)
    write(path, text)

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    import os
    tmp = sys.argv[1] if len(sys.argv) > 1 else "/tmp/scripten_demo"
    print(f"Injecting linkage in {tmp}/")
    inject_prolog(os.path.join(tmp, "FamilyProlog.j"))
    inject_snobol4(os.path.join(tmp, "FamilySnobol4.j"))
    inject_icon(os.path.join(tmp, "FamilyIcon.j"))
    print("Done.")
