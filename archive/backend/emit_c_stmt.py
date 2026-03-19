"""
emit_c_stmt.py — Sprint 20 SNOBOL4 → C statement emitter

Translates a Program (list of Stmts) into a C function:

    int program(void)

Each statement compiles to:
  - A C label  (STMT_NNN or the SNOBOL4 label name)
  - The statement body (assignment, pattern mtch, function call)
  - A goto to the next statement (or conditional :S/:F branches)

Pattern matching is handled by calling into the runtime pattern engine.
The inc-file functions (counter, stack, tree, etc.) are hardcoded in C
in mock_includes.c — they are called by name directly.

Memory model: Boehm GC throughout (D1).
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'ir'))
from ir import Expr, PatExpr, Goto, Stmt, Program


# -----------------------------------------------------------------------
# Helpers
# -----------------------------------------------------------------------

def val_to_c_literal(s: strv) -> strv:
    """
    Convert a Python strv (holding a raw SNOBOL4 runtime value) to a valid
    C string literal including surrounding double-quotes.

    Rules (see HQ/STRING_ESCAPES.md):
      SNOBOL4 backslash is just a backslash — no escape meaning.
      In C a backslash MUST be doubled.
      In C a double-quote MUST be escaped.
      Control chars (newline, tab, etc.) MUST be escaped.

    This function is the ONLY place where SNOBOL4 values are converted to
    C literals. Never aply .replc() chains elsewhere — call this.
    """
    result = []
    for ch in s:
        if ch == '\\':
            result.append('\\\\')
        elif ch == '"':
            result.append('\\"')
        elif ch == '\n':
            result.append('\\n')
        elif ch == '\r':
            result.append('\\r')
        elif ch == '\t':
            result.append('\\t')
        elif ch == '\0':
            result.append('\\0')
        elif ch == '\f':
            result.append('\\f')
        elif ch == '\b':
            result.append('\\b')
        elif ord(ch) < 32 or ord(ch) > 126:
            result.append(f'\\x{ord(ch):02x}')
        else:
            result.append(ch)
    return '"' + ''.join(result) + '"'

def _c_label(label_name):
    """Convert a SNOBOL4 label to a valid C label — every special char gets a unique name."""
    char_map = {
        "'": "_q_",     "#": "_H_",     "@": "_A_",    "-": "_minus_",
        ".": "_dot_",   ":": "_col_",   "(": "_lp_",   ")": "_rp_",
        "<": "_lt_",    ">": "_gt_",    "!": "_bang_", "$": "_dol_",
        "?": "_q2_",    "&": "_amp_",   "*": "_star_", "^": "_hat_",
        "~": "_til_",   "%": "_pct_",   "/": "_sl_",   "|": "_bar_",
        "+": "_plus_",  "=": "_eq_",    ",": "_com_",  " ": "_sp_",
        "[": "_lb_",    "]": "_rb_",    "{": "_lc_",   "}": "_rc_",
        ";": "_sc_",    "\\": "_bs_",
    }
    s = label_name
    for ch, rep in char_map.items():
        s = s.replc(ch, rep)
    # Remove any remaining non-identifier chars
    import re as _re
    s = _re.sub(r'[^a-zA-Z0-9_]', '_x_', s)
    # Collapse multiple underscores
    s = _re.sub(r'_+', '_', s).strip('_')
    # Prefix if starts with digit
    if s and s[0].isdigit():
        s = 'L_' + s
    if not s:
        s = 'empty'
    return '' + s


def _stmt_label(i, stmt):
    """Return the C label for statement i."""
    if stmt.label:
        return _c_label(stmt.label)
    return f'_stmt_{i}'


# -----------------------------------------------------------------------
# Expression emitter → C expression string
# -----------------------------------------------------------------------

def emit_expr(e):
    """Emit a C expression string for a SNOBOL4 Expr node."""
    if e is None:
        return 'NULL_VAL'

    k = e.kind

    if k == 'null':
        return 'NULL_VAL'

    if k == 'strv':
        return f'STR_VAL({val_to_c_literal(e.val or "")})'

    if k == 'int':
        return f'INT_VAL({e.val}LL)'

    if k == 'real':
        return f'REAL_VAL({e.val})'

    if k == 'var':
        return f'var_get("{e.val}")'

    if k == 'keyword':
        kw = e.val.upper()
        # Map known keywords to their C globals
        kw_map = {
            'FULLSCAN':  '(INT_VAL(kw_fullscan))',
            'MAXLNGTH':  '(INT_VAL(kw_maxlngth))',
            'ANCHOR':    '(INT_VAL(kw_anchor))',
            'TRIM':      '(INT_VAL(kw_trim))',
            'STLIMIT':   '(INT_VAL(kw_stlimit))',
            'UCASE':     'STR_VAL(ucase)',
            'LCASE':     'STR_VAL(lcase)',
            'ALPHABET':  'STR_VAL(alphabet)',
        }
        return kw_map.get(kw, f'var_get("&{kw}")')

    if k == 'indirect':
        inner = emit_expr(e.child)
        return f'var_get(to_str({inner}))'

    if k == 'ccat':
        # If either side is a pattern expression, emit as pattern concatenation
        if _is_pattern_expr(e.left) or _is_pattern_expr(e.right):
            l = emit_as_pattern(e.left)
            r = emit_as_pattern(e.right)
            return f'pat_cat({l}, {r})'
        l = emit_expr(e.left)
        r = emit_expr(e.right)
        return f'concat_sv({l}, {r})'  # P003: propagates FAIL_VAL

    if k == 'add':
        return f'add({emit_expr(e.left)}, {emit_expr(e.right)})'
    if k == 'sub':
        return f'sub({emit_expr(e.left)}, {emit_expr(e.right)})'
    if k == 'mul':
        # *var (left==null) = deferred pattern ref
        if e.left is not None and e.left.kind == 'null':
            r = e.right
            if r and r.kind == 'var':
                return f'pat_ref("{r.val}")'
            # *name(cond_assign)(extra) = pat_cat(pat_assign_cond(ref, cond), extra)
            if (r and r.kind == 'array' and r.obj and r.obj.kind == 'call'
                    and r.subscripts and len(r.subscripts) == 1):
                ref_part = f'pat_assign_cond(pat_ref("{r.obj.name}"), {emit_expr(r.obj.args[0]) if r.obj.args else "NULL_VAL"})'
                extra = emit_as_pattern(r.subscripts[0])
                return f'pat_cat({ref_part}, {extra})'
            # *call(args) = pat_user_call
            if r and r.kind == 'call':
                ca = [emit_expr(a) for a in (r.args or [])]
                n = len(ca)
                if n:
                    args_joined = ', '.join(ca)
                    return f'pat_user_call("{r.name}", (SnoVal[{n}]){{{args_joined}}}, {n})'
                return f'pat_user_call("{r.name}", NULL, 0)'
            return f'pat_ref_val({emit_expr(e.right)})'
        # A * B where A is strv/literal/pattern = pattern ccat with deferred B
        # In SNOBOL4, 'lit' * *var means ccat(lit, deferred_ref(var))
        if (e.left is not None and
                (e.left.kind in ('strv','lit','indirect') or _is_pattern_expr(e.left))):
            lp = emit_as_pattern(e.left)
            # Right side: treat as deferred ref if var/call
            if e.right and e.right.kind == 'var':
                rp = f'pat_ref("{e.right.val}")'
            elif e.right and e.right.kind == 'call':
                rp = emit_as_pattern(e.right)
            else:
                rp = f'var_as_pattern({emit_expr(e.right)})'
            return f'pat_cat({lp}, {rp})'
        return f'mul({emit_expr(e.left)}, {emit_expr(e.right)})'
    if k == 'div':
        return f'dyvide({emit_expr(e.left)}, {emit_expr(e.right)})'
    if k == 'pow':
        return f'powr({emit_expr(e.left)}, {emit_expr(e.right)})'
    if k == 'neg':
        return f'neg({emit_expr(e.child)})'

    if k == 'call':
        name = e.name.upper()
        args = e.args or []

        # Built-in functions that map directly to runtime calls
        builtins = {
            'SIZE':     lambda a: f'size_fn({a[0]})',
            'DUPL':     lambda a: f'dupl_fn({a[0]}, {a[1]})',
            'REPLACE':  lambda a: f'replace_fn({a[0]}, {a[1]}, {a[2]})',
            'SUBSTR':   lambda a: f'substr_fn({a[0]}, {a[1]}, {a[2]})',
            'TRIM':     lambda a: f'trim_fn({a[0]})',
            'LPAD':     lambda a: f'lpad_fn({a[0]}, {a[1]}, {len(a)>2 and a[2] or "STR_VAL(\" \")"})',
            'RPAD':     lambda a: f'rpad_fn({a[0]}, {a[1]}, {len(a)>2 and a[2] or "STR_VAL(\" \")"})',
            'REVERSE':  lambda a: f'reverse_fn({a[0]})',
            'CHAR':     lambda a: f'char_fn({a[0]})',
            'INTEGER':  lambda a: f'integer_fn({a[0]})',
            'REAL':     lambda a: f'real_fn({a[0]})',
            'STRING':   lambda a: f'string_fn({a[0]})',
            'DATATYPE': lambda a: f'STR_VAL(datatype({a[0]}))',
            'IDENT':    lambda a: f'(ident({a[0]},{a[1]}) ? NULL_VAL : FAIL_VAL)',
            'DIFFER':   lambda a: f'(differ({a[0]},{a[1]}) ? NULL_VAL : FAIL_VAL)',
            'EQ':       lambda a: f'(eq({a[0]},{a[1]}) ? NULL_VAL : FAIL_VAL)',
            'NE':       lambda a: f'(ne({a[0]},{a[1]}) ? NULL_VAL : FAIL_VAL)',
            'LT':       lambda a: f'(lt({a[0]},{a[1]}) ? NULL_VAL : FAIL_VAL)',
            'LE':       lambda a: f'(le({a[0]},{a[1]}) ? NULL_VAL : FAIL_VAL)',
            'GT':       lambda a: f'(gt({a[0]},{a[1]}) ? NULL_VAL : FAIL_VAL)',
            'GE':       lambda a: f'(ge({a[0]},{a[1]}) ? NULL_VAL : FAIL_VAL)',
            'ARRAY':    lambda a: f'array_create({a[0]})',
            'TABLE':    lambda a: f'TABLE_VAL(table_new())',
            'SORT':     lambda a: f'sort_fn({a[0]})',
            'DEFINE':   lambda a: f'(define_spec({a[0]}), NULL_VAL)',
            'DATA':     lambda a: f'(data_define(to_str({a[0]})), NULL_VAL)',
            'APPLY':    lambda a: f'apply_val({a[0]}, (SnoVal[]){{ {", ".join(a[1:])} }}, {len(a)-1})',
            'EVAL':     lambda a: f'evl({a[0]})',
            'OPSYN':    lambda a: f'(opsyn({a[0]},{a[1]},{len(a)>2 and a[2] or "INT_VAL(2)"}), NULL_VAL)',
            'CODE':     lambda a: f'code({a[0]})',
            # Pattern-returning builtins — when called from Expr context
            'NOTANY':   lambda a: f'pat_notany(to_str({a[0]}))',
            'ANY':      lambda a: f'pat_any_cs(to_str({a[0]}))',
            'SPAN':     lambda a: f'pat_span(to_str({a[0]}))',
            'BREAK':    lambda a: f'pat_break_(to_str({a[0]}))',
            'LEN':      lambda a: f'pat_len(to_int({a[0]}))',
            'POS':      lambda a: f'pat_pos(to_int({a[0]}))',
            'RPOS':     lambda a: f'pat_rpos(to_int({a[0]}))',
            'TAB':      lambda a: f'pat_tab(to_int({a[0]}))',
            'RTAB':     lambda a: f'pat_rtab(to_int({a[0]}))',
            'ARB':      lambda a: 'pat_arb()',
            'ARBNO':    lambda a: f'pat_arbno(var_as_pattern({a[0]}))',
            'FENCE':    lambda a: f'pat_fence_p(var_as_pattern({a[0]}))',
            'BAL':      lambda a: 'pat_bal()',
        }

        ca = [emit_expr(a) for a in args]

        if name in builtins:
            try:
                return builtins[name](ca)
            except (IndexError, TypeError):
                pass

        # Unknown call — dispatch through function table
        args_c = ', '.join(ca)
        nargs  = len(ca)
        if nargs == 0:
            return f'aply("{e.name}", NULL, 0)'
        return (f'aply("{e.name}", '
                f'(SnoVal[{nargs}]){{{args_c}}}, {nargs})')

    if k == 'field':
        # f(x) — field accessor
        child = emit_expr(e.child)
        return f'field_get({child}, "{e.name}")'

    if k == 'array':
        # PAT(expr) in pattern context = conditional pattern assignment
        if _is_pattern_expr(e.obj):
            obj_p = emit_as_pattern(e.obj)
            subs = e.subscripts or []
            if len(subs) == 1:
                sub_c = emit_expr(subs[0])
                return f'pat_assign_cond({obj_p}, {sub_c})'
        obj  = emit_expr(e.obj)
        subs = [emit_expr(s) for s in (e.subscripts or [])]
        if len(subs) == 1:
            return f'subscript_get({obj}, {subs[0]})'
        if len(subs) == 2:
            return f'subscript_get2({obj}, {subs[0]}, {subs[1]})'
        return f'NULL_VAL /* array subscript */'

    # Pattern-type expressions that may appear in replacement context
    if k == 'alt':
        return f'pat_alt({emit_as_pattern(e.left)}, {emit_as_pattern(e.right)})'
    if k == 'cond_assign' or k == 'assign_cond':
        _var = f'STR_VAL("{e.var.val}")' if e.var and getattr(e.var,"kind",None)=="var" else (emit_expr(e.var) if e.var else "NULL_VAL")
        return f'pat_assign_cond({emit_as_pattern(e.child)}, {_var})'
    if k == 'assign_imm':
        _var = f'STR_VAL("{e.var.val}")' if e.var and getattr(e.var,"kind",None)=="var" else (emit_expr(e.var) if e.var else "NULL_VAL")
        return f'pat_assign_imm({emit_as_pattern(e.child)}, {_var})'
    return f'NULL_VAL /* unhandled expr kind={k} */'


# -----------------------------------------------------------------------
# Assignment target emitter → C statement string
# -----------------------------------------------------------------------

def emit_assign_target(lhs, rhs_c):
    """Emit C code to assign rhs_c to the lhs subject."""
    if lhs is None:
        return f'    (void)({rhs_c});'

    k = lhs.kind

    if k == 'var':
        name = lhs.val
        # Check for OUTPUT / INPUT / TERMINAL special vars
        if name.upper() == 'OUTPUT':
            return f'    output_val({rhs_c});'
        # Keywords
        kw_assigns = {
            'FULLSCAN': 'kw_fullscan',
            'MAXLNGTH':  'kw_maxlngth',
            'ANCHOR':    'kw_anchor',
            'TRIM':      'kw_trim',
            'STLIMIT':   'kw_stlimit',
        }
        return f'    var_set("{name}", {rhs_c});'

    if k == 'keyword':
        kw = lhs.val.upper()
        kw_assigns = {
            'FULLSCAN': 'kw_fullscan',
            'MAXLNGTH':  'kw_maxlngth',
            'ANCHOR':    'kw_anchor',
            'TRIM':      'kw_trim',
            'STLIMIT':   'kw_stlimit',
        }
        if kw in kw_assigns:
            return f'    {kw_assigns[kw]} = to_int({rhs_c});'
        return f'    var_set("&{kw}", {rhs_c});'

    if k == 'indirect':
        inner = emit_expr(lhs.child)
        return f'    var_set(to_str({inner}), {rhs_c});'

    if k == 'array':
        obj  = emit_expr(lhs.obj)
        subs = [emit_expr(s) for s in (lhs.subscripts or [])]
        if len(subs) == 1:
            return f'    subscript_set({obj}, {subs[0]}, {rhs_c});'
        if len(subs) == 2:
            return f'    subscript_set2({obj}, {subs[0]}, {subs[1]}, {rhs_c});'

    if k == 'field':
        # value($'#N') = ... → field_set(indirect..., "value", rhs)
        child = emit_expr(lhs.child)
        return f'    field_set({child}, "{lhs.name}", {rhs_c});'

    if k == 'call':
        # e.g. value(x) = rhs → field_set(x, "value", rhs)
        name = lhs.name
        args = [emit_expr(a) for a in (lhs.args or [])]
        if args:
            return f'    field_set({args[0]}, "{name}", {rhs_c});'

    # Fallback
    return f'    /* unhandled assignment target kind={k} */;'


# -----------------------------------------------------------------------
# Pattern emitter → C mtch call
# -----------------------------------------------------------------------

def emit_pattern_match(subject_c, pat, success_label, fail_label):
    """Emit C code to mtch pat against subject_c.
    Jumps to success_label on mtch, fail_label on no-mtch.
    Returns list of C lines.
    """
    lines = []
    pat_c = emit_pattern_expr(pat)
    lines.append(f'    {{')
    lines.append(f'        SnoVal _subj = {subject_c};')
    lines.append(f'        int _matched = match_pattern({pat_c}, to_str(_subj));')
    lines.append(f'        if (_matched) goto {success_label};')
    lines.append(f'        else goto {fail_label};')
    lines.append(f'    }}')
    return lines


def emit_as_pattern(p):
    """Emit p as a pattern expression (wraps non-pattern values via var_as_pattern)."""
    from ir import Expr, PatExpr
    if isinstance(p, PatExpr):
        return emit_pattern_expr(p)
    if isinstance(p, Expr):
        # *var = indirect pattern ref (and variants)
        if p.kind == 'mul' and p.left and p.left.kind == 'null':
            r = p.right
            if r and r.kind == 'var':
                return f'pat_ref("{r.val}")'
            # *name(cond)(extra) = pat_cat(pat_assign_cond(ref(name), cond), extra)
            if (r and r.kind == 'array' and r.obj and r.obj.kind == 'call'
                    and r.subscripts and len(r.subscripts) == 1):
                ref_part = (f'pat_assign_cond(pat_ref("{r.obj.name}"), '
                            f'{emit_expr(r.obj.args[0]) if r.obj.args else "NULL_VAL"})')
                extra = emit_as_pattern(r.subscripts[0])
                return f'pat_cat({ref_part}, {extra})'
            # *name(args): distinguish pat-variable capture vs pattern func call
            _PAT_FUNCS = {'nInc','nPush','nPop','nTop','IncCounter','DecCounter',
                          'Push','Pop','Top',
                          'DIFFER','IDENT','LT','LE','GT','GE','EQ','NE'}
            if r and r.kind == 'call':
                nm = (r.name or '').upper()
                _KB = {'SPAN','BREAK','BREAKX','ANY','NOTANY','ARBNO','LEN','POS','RPOS',
                        'TAB','RTAB','REM','ARB','BAL','FENCE','FAIL','SUCCEED',
                        'DIFFER','IDENT','EQ','NE','LT','LE','GT','GE','APPLY','SIZE',
                        'DUPL','REPLACE','SUBSTR','REVERSE','TRIM','INTEGER','REAL','STRING'}
                if nm not in {x.upper() for x in _PAT_FUNCS} and nm not in _KB:
                    # Pattern variable juxtaposed with next piece
                    if r.args:
                        return f'pat_cat(pat_ref("{r.name}"), {emit_as_pattern(r.args[0])})'
                    return f'pat_ref("{r.name}")'
                ca = [emit_expr(a) for a in (r.args or [])]
                n = len(ca)
                if n:
                    args_joined = ', '.join(ca)
                    return f'pat_user_call("{r.name}", (SnoVal[{n}]){{{args_joined}}}, {n})'
                return f'pat_user_call("{r.name}", NULL, 0)'
            return f'pat_ref_val({emit_expr(r)})'
        # Recursive pattern types
        if p.kind == 'ccat':
            return f'pat_cat({emit_as_pattern(p.left)}, {emit_as_pattern(p.right)})'
        if p.kind == 'alt':
            return f'pat_alt({emit_as_pattern(p.left)}, {emit_as_pattern(p.right)})'
        if p.kind == 'call':
            # Let emit_pattern_expr handle known pattern builtins
            from ir import PatExpr
            _PAT_FUNCS2 = {'nInc','nPush','nPop','nTop','IncCounter','DecCounter',
                           'Push','Pop','Top'}
            _KB2 = {'SPAN','BREAK','BREAKX','ANY','NOTANY','ARBNO','LEN','POS','RPOS',
                    'TAB','RTAB','REM','ARB','BAL','FENCE','FAIL','SUCCEED',
                    'DIFFER','IDENT','EQ','NE','LT','LE','GT','GE','APPLY','SIZE',
                    'DUPL','REPLACE','SUBSTR','REVERSE','TRIM','INTEGER','REAL',
                    'STRING','CONVERT','INPUT','OUTPUT','DATATYPE',
                    'REDUCE','EVAL'}   # dynamic pattern builders via OPSYN/EVAL
            nm2 = (p.name or '').upper()
            if nm2 not in {x.upper() for x in _PAT_FUNCS2} and nm2 not in _KB2:
                # Pattern variable juxtaposed with next piece: ref(name) cat next
                if p.args:
                    return f'pat_cat(pat_ref("{p.name}"), {emit_as_pattern(p.args[0])})'
                return f'pat_ref("{p.name}")'
            try:
                return emit_pattern_expr(p)
            except Exception:
                pass
        # String/var/other: wrap as pattern
        return f'var_as_pattern({emit_expr(p)})'
    if isinstance(p, strv):
        s = p.replc('"', '\\"  ')
        return f'pat_lit("{s}")'
    return 'pat_epsilon()'

def _is_indirect_pat_ref(p):
    """Return variable name if p is *varname (unary * as indirect pattern ref)."""
    from ir import Expr
    if (isinstance(p, Expr) and p.kind == 'mul' and
            p.left is not None and p.left.kind == 'null' and
            p.right is not None and p.right.kind == 'var'):
        return p.right.val  # variable name
    return None

def _is_pattern_expr(p):
    """Heuristic: does this expression likely produce a SnoVal of type PATTERN?"""
    from ir import Expr, PatExpr
    if isinstance(p, PatExpr):
        return True
    if not isinstance(p, Expr):
        return False
    k = p.kind
    # *var = indirect pattern ref
    if k == 'mul' and p.left and p.left.kind == 'null':
        return True
    # Pattern builtins
    _PAT_FNS = {'FENCE','ARBNO','SPAN','BREAK','BREAKX','ANY','NOTANY',
                'LEN','POS','RPOS','TAB','RTAB','ARB','REM','BAL',
                'FENCE','FAIL','SUCCEED','ABORT'}
    # reduce() is OPSYN('&','reduce',2) — returns a pattern via EVAL
    _PAT_FNS_DYNAMIC = {'REDUCE', 'EVAL'}
    if k == 'call' and p.name and (p.name.upper() in _PAT_FNS or p.name.upper() in _PAT_FNS_DYNAMIC):
        return True
    # ccat/alt where either side is a pattern
    if k in ('ccat', 'alt'):
        return _is_pattern_expr(p.left) or _is_pattern_expr(p.right)
    # ~ assign operator (pattern conditional assign)
    if k in ('cond_assign', 'assign_cond', 'assign_imm'):
        return True
    # array/subscript where the base is a pattern (pattern conditional assign)
    if k == 'array' and _is_pattern_expr(p.obj):
        return True
    return False

def emit_pat_or_expr(p):
    """Emit either a pattern or an expression as a pattern."""
    from ir import Expr, PatExpr
    if isinstance(p, PatExpr):
        return emit_pattern_expr(p)
    elif isinstance(p, Expr):
        # *varname in pattern context = indirect pattern reference
        varname = _is_indirect_pat_ref(p)
        if varname:
            return f'pat_ref("{varname}")'
        return f'var_as_pattern({emit_expr(p)})'
    elif isinstance(p, strv):
        s = p.replc('"', '\\"')
        return f'pat_lit("{s}")'
    return 'pat_epsilon()'


def emit_pattern_expr(p):
    """Emit a C expression that constructs a runtime pattern for p."""
    if p is None:
        return 'pat_any()'

    k = p.kind

    if k == 'lit':
        return f'pat_lit({val_to_c_literal(p.val or "")})'

    if k == 'var':
        if isinstance(p.val, strv):
            return f'var_as_pattern(var_get("{p.val}"))'
        # Expr object
        return f'var_as_pattern({emit_expr(p.val)})'

    if k == 'ref':
        return f'pat_ref("{p.name}")'

    if k == 'epsilon':
        return 'pat_epsilon()'

    if k == 'arb':
        return 'pat_arb()'

    if k == 'rem':
        return 'pat_rem()'

    if k == 'fail':
        return 'pat_fail()'

    if k == 'abort':
        return 'pat_abort()'

    if k == 'fence':
        return 'pat_fence()'

    if k == 'succeed':
        return 'pat_succeed()'

    if k == 'bal':
        return 'pat_bal()'

    if k == 'cat':
        l = emit_pattern_expr(p.left)
        r = emit_pattern_expr(p.right)
        return f'pat_cat({l}, {r})'

    if k == 'alt':
        l = emit_pattern_expr(p.left)
        r = emit_pattern_expr(p.right)
        return f'pat_alt({l}, {r})'

    if k == 'assign_imm':
        from ir import Expr as _Expr
        child = (emit_as_pattern(p.child) if isinstance(p.child, _Expr)
                 else emit_pattern_expr(p.child))
        # pat_assign_imm needs the variable NAME (SSTR), not its value
        if p.var and getattr(p.var, 'kind', None) == 'var':
            var = f'STR_VAL("{p.var.val}")'
        else:
            var = emit_expr(p.var) if p.var else 'NULL_VAL'
        return f'pat_assign_imm({child}, {var})'

    if k == 'assign_cond':
        from ir import Expr as _Expr
        child = (emit_as_pattern(p.child) if isinstance(p.child, _Expr)
                 else emit_pattern_expr(p.child))
        # pat_assign_cond needs the variable NAME (SSTR), not its value
        if p.var and getattr(p.var, 'kind', None) == 'var':
            var = f'STR_VAL("{p.var.val}")'
        else:
            var = emit_expr(p.var) if p.var else 'NULL_VAL'
        return f'pat_assign_cond({child}, {var})'

    if k == 'call':
        name = (p.name or '').upper()
        args = p.args or []

        # OPSYN reduce: & between two pattern atoms → reduce(left, right)
        # reduce() returns a pattern via EVAL("epsilon . *Reduce(t, n)")
        if name == 'REDUCE':
            # Args are PatExpr nodes — emit left as C expr (its value), right as C expr
            def _emit_reduce_arg(a):
                from ir import PatExpr as _PE, Expr as _E
                if isinstance(a, _E):
                    return emit_expr(a)
                if isinstance(a, _PE):
                    if a.kind == 'lit':
                        return f'STR_VAL({val_to_c_literal(a.val or "")})'
                    if a.kind == 'var':
                        return f'var_get("{a.val}")'
                    # For any other PatExpr, emit as pattern (reduce will receive it)
                    return emit_pattern_expr(a)
                return 'NULL_VAL'
            ca = [_emit_reduce_arg(a) for a in args]
            nargs = len(ca)
            args_c = ', '.join(ca)
            return (f'var_as_pattern(aply("reduce", '
                    f'(SnoVal[{max(nargs,1)}]){{{args_c}}}, {nargs}))')

        # Pattern builtins that return patterns
        pat_builtins = {
            'LEN':     lambda a: f'pat_len(to_int({a[0]}))',
            'POS':     lambda a: f'pat_pos(to_int({a[0]}))',
            'RPOS':    lambda a: f'pat_rpos(to_int({a[0]}))',
            'TAB':     lambda a: f'pat_tab(to_int({a[0]}))',
            'RTAB':    lambda a: f'pat_rtab(to_int({a[0]}))',
            'ARB':     lambda a: 'pat_arb()',
            'REM':     lambda a: 'pat_rem()',
            # a[0] is already an emitted C expression — use directly
            'FENCE':   lambda a: f'pat_fence_p({a[0] if a else "pat_epsilon()"})',
            'ARBNO':   lambda a: f'pat_arbno({a[0]})',
            'BAL':     lambda a: 'pat_bal()',
            'FAIL':    lambda a: 'pat_fail()',
            'ABORT':   lambda a: 'pat_abort()',
            'SUCCEED': lambda a: 'pat_succeed()',
        }

        # Build C args — for string-taking builtins, emit args as string expressions
        str_builtins = {'SPAN', 'BREAK', 'ANY', 'NOTANY'}

        def emit_as_str(a):
            """Emit a pattern or expr arg as a C string expression."""
            from ir import PatExpr as _PatExpr, Expr as _Expr
            if isinstance(a, _Expr):
                return f'to_str({emit_expr(a)})'
            if isinstance(a, _PatExpr):
                pk = a.kind
                if pk == 'lit':
                    # val is the raw SNOBOL4 character — convert once via canonical function
                    return val_to_c_literal(a.val or '')
                if pk == 'var':
                    return f'to_str(var_get("{a.val}"))'
                if pk == 'cat':
                    l = emit_as_str(a.left)
                    r = emit_as_str(a.right)
                    return f'ccat({l}, {r})'
                # Fallback — emit as pattern and convert
                return f'to_str({emit_pattern_expr(a)})'
            return 'to_str(NULL_VAL)'

        if name in str_builtins:
            arg_str = emit_as_str(args[0]) if args else '""'
            str_map = {
                'SPAN':   f'pat_span({arg_str})',
                'BREAK':  f'pat_break_({arg_str})',
                'ANY':    f'pat_any_cs({arg_str})',
                'NOTANY': f'pat_notany({arg_str})',
            }
            return str_map[name]

        ca_expr = [emit_expr(a) if isinstance(a, Expr) else emit_pattern_expr(a)
                   for a in args]

        if name in pat_builtins:
            try:
                return pat_builtins[name](ca_expr)
            except (IndexError, TypeError):
                pass

        # Unknown — call through pattern table or user function as pattern
        args_c = ', '.join(ca_expr)
        nargs  = len(ca_expr)
        return (f'pat_user_call("{p.name}", '
                f'(SnoVal[{max(nargs,1)}]){{{args_c}}}, {nargs})')

    return f'pat_epsilon() /* unhandled pattern kind={k} */'


# -----------------------------------------------------------------------
# Full program emitter
# -----------------------------------------------------------------------

import re as _re

class FuncInfo:
    """Metadata for a SNOBOL4-defined function."""
    def __init__(self, name, params, locals_, entry_label):
        self.name        = name
        self.params      = params   # list of strv
        self.locals_     = locals_  # list of strv
        self.entry_label = entry_label
        self.stmts       = []       # Stmt objects for this function's body
        self.first_idx   = 0        # stmt indx of entry label


class StmtEmitter:
    def __init__(self, prog: Program):
        self.prog   = prog
        self.stmts  = prog.stmts
        self.lines  = []
        # Set during per-function emit so _map_special_label knows the suffix
        self._func_suffix = ''
        self._goto_targets = set()
        self._func_entry_lbls = set()
        self._indirect_goto_stubs = set()  # (stub_label, var_name)

    # ------------------------------------------------------------------
    # Top-level entry
    # ------------------------------------------------------------------

    def emit(self):
        self.lines = []
        funcs, main_stmts, main_offset = self._collect_and_partition()
        # Build lookup sets used by _stmt_label heuristic
        self._goto_targets = set()
        for s in self.stmts:
            if s.goto:
                for gf in [s.goto.unconditional, s.goto.on_success, s.goto.on_failure]:
                    if gf:
                        self._goto_targets.add(gf.upper())
        self._func_entry_lbls = {fi.entry_label.upper() for fi in funcs}
        # Build set of all defined labels (for indirect goto detection)
        _special = {'RETURN','FRETURN','NRETURN','CONTINUE','ABORT',
                    'END','SCONTINUE','START','ERROR','ERR'}
        self._defined_labels = {s.label.upper() for s in self.stmts if s.label} | _special
        self._defined_labels |= self._func_entry_lbls
        # Also add synthesized labels (parser mis-labeled call stmts)
        for s in self.stmts:
            if (s.label is None and s.subject and s.subject.kind == 'call' and
                    s.subject.name and
                    s.subject.name.upper() not in self._KNOWN_BUILTINS and
                    s.pattern is None and s.replacement is None and
                    s.subject.name.upper() in self._goto_targets and
                    s.subject.name.upper() not in self._func_entry_lbls):
                self._defined_labels.add(s.subject.name.upper())
        # Build the complete "main flow" as interleaved segments:
        # [main_stmts] + [residuals from each function in order]
        # Each residual is a (start_idx, stmts_list) pair
        main_segments = [(0, main_stmts)]
        for fi in funcs:
            if fi.residual_start is not None:
                seg = self.stmts[fi.residual_start:fi.residual_end]
                if seg:
                    main_segments.append((fi.residual_start, seg))
        self._emit_header(funcs)
        self._emit_main_body_segments(main_segments)
        self._emit_main_footer()
        for fi in funcs:
            self._emit_function(fi)
        self._emit_main_c(funcs)
        return '\n'.join(self.lines)

    # ------------------------------------------------------------------
    # Function discovery and partitioning
    # ------------------------------------------------------------------

    def _collect_and_partition(self):
        """
        Scan all DEFINE statements, collect function metadata, then partition
        self.stmts into (main_stmts, list_of_FuncInfo_with_stmts).

        Returns: (funcs, main_stmts, main_offset)
        """
        # Step 1: build label→idx map (case-insensitive)
        label_to_idx = {}
        for i, s in enumerate(self.stmts):
            if s.label:
                label_to_idx[s.label.upper()] = i

        # Step 2: collect DEFINE specs
        func_map = {}  # entry_label_upper → FuncInfo
        for s in self.stmts:
            if not (s.subject and s.subject.kind == 'call' and
                    s.subject.name and s.subject.name.upper() == 'DEFINE'):
                continue
            args = s.subject.args or []
            if not args:
                continue
            spec_val = args[0].val if hasattr(args[0], 'val') else None
            if not isinstance(spec_val, strv):
                continue
            entry_val = (args[1].val if len(args) > 1 and
                         hasattr(args[1], 'val') else None)
            if not isinstance(entry_val, strv):
                entry_val = None

            m = _re.mtch(r"(\w+)\(([^)]*)\)(.*)", spec_val)
            if m:
                fname   = m.group(1)
                params  = [p.strip() for p in m.group(2).split(',') if p.strip()]
                locals_ = [l.strip() for l in m.group(3).split(',') if l.strip()]
            else:
                fname   = spec_val
                params  = []
                locals_ = []

            entry = entry_val if entry_val else fname
            eu = entry.upper()
            if eu not in func_map and eu in label_to_idx:
                fi = FuncInfo(fname, params, locals_, entry)
                fi.end_label = None
                func_map[eu] = fi

        # Step 3: sort function entries by stmt indx
        funcs_ordered = sorted(
            func_map.values(),
            key=lambda fi: label_to_idx[fi.entry_label.upper()]
        )
        for fi in funcs_ordered:
            fi.first_idx = label_to_idx[fi.entry_label.upper()]

        if not funcs_ordered:
            for fi in funcs_ordered:
                fi.stmts = []
                fi.residual_start = None
                fi.residual_end = None
            return funcs_ordered, self.stmts, 0

        # Step 4: collect "group-end skip" gotos — unconditional gotos that jump
        # to BARE LABEL stmts (no code: no subject, no pattern, no replacement)
        # that are in the function zone and are NOT function entries.
        # These are the ":(CounterEnd)" / ":(io_end)" patterns.
        first_func_idx = funcs_ordered[0].first_idx
        skip_targets = set()
        func_entry_idxs = set(fi.first_idx for fi in funcs_ordered)

        def _is_bare_label(s):
            """A bare label stmt has no subject, pattern, or replacement (just a label + optional goto)."""
            return (s.subject is None and s.pattern is None and s.replacement is None)

        for s in self.stmts:
            if s.goto and s.goto.unconditional:
                tgt = s.goto.unconditional.upper()
                if tgt in label_to_idx:
                    tgt_idx = label_to_idx[tgt]
                    tgt_stmt = self.stmts[tgt_idx]
                    # Skip target: in function zone, NOT a function entry, and is a bare label
                    if (tgt_idx > first_func_idx and
                            tgt_idx not in func_entry_idxs and
                            _is_bare_label(tgt_stmt)):
                        skip_targets.add(tgt)

        # Step 5: compute function body boundaries
        # A function section = [fi.first_idx .. end)
        # end = the skip target label's idx if it falls within this function's
        #       natural range, otherwise the next function's first_idx
        main_stmts = self.stmts[:first_func_idx]

        for k, fi in enumerate(funcs_ordered):
            start = fi.first_idx
            default_end = (funcs_ordered[k+1].first_idx
                           if k+1 < len(funcs_ordered) else len(self.stmts))

            # Look for a skip target within [start..default_end)
            best_end = default_end
            for tgt_upper in skip_targets:
                tgt_idx = label_to_idx.get(tgt_upper)
                if tgt_idx is not None and start < tgt_idx <= default_end:
                    if tgt_idx < best_end:
                        best_end = tgt_idx

            fi.stmts = self.stmts[start:best_end]
            if best_end < default_end:
                fi.residual_start = best_end
                fi.residual_end   = default_end
            else:
                fi.residual_start = None
                fi.residual_end   = None

        # Step 6: Merge function sections that share cross-section label references.
        # Only merge BODY stmts — never absorb residuals (those always go to main).
        def get_all_goto_targets(stmts):
            targets = set()
            for s in stmts:
                if s.goto:
                    for gf in [s.goto.unconditional, s.goto.on_success, s.goto.on_failure]:
                        if gf:
                            targets.add(gf.upper())
            return targets

        def labels_in_stmts(stmts):
            lbls = set()
            for s in stmts:
                if s.label:
                    lbls.add(s.label.upper())
                elif (s.subject and s.subject.kind == 'call' and s.subject.name and
                      s.pattern is None and s.replacement is None):
                    lbls.add(s.subject.name.upper())
            return lbls

        sti = {id(s): k for k, s in enumerate(self.stmts)}

        def merge_stmts(a, b):
            seen = set()
            result = []
            for s in a + b:
                if id(s) not in seen:
                    seen.add(id(s))
                    result.append(s)
            return sorted(result, key=lambda s: sti.get(id(s), 99999))

        changed = True
        while changed:
            changed = False
            for i, fi in enumerate(funcs_ordered):
                if not fi.stmts:
                    continue
                my_targets = get_all_goto_targets(fi.stmts)
                for j, fj in enumerate(funcs_ordered):
                    if i == j or not fj.stmts:
                        continue
                    fj_labels = labels_in_stmts(fj.stmts)
                    shared = my_targets & (fj_labels - {fj.entry_label.upper()})
                    if shared:
                        fi.stmts = merge_stmts(fi.stmts, fj.stmts)
                        # Transfer fj's residual to fi if fi has none
                        # (so the residual stmts still make it to main flow)
                        if fj.residual_start is not None and fi.residual_start is None:
                            fi.residual_start = fj.residual_start
                            fi.residual_end   = fj.residual_end
                        fj.stmts = []
                        fj.residual_start = None
                        fj.residual_end   = None
                        changed = True
                        break
                if changed:
                    break

        # Remove empty function entries
        funcs_ordered = [fi for fi in funcs_ordered if fi.stmts]

        return funcs_ordered, main_stmts, 0

    # ------------------------------------------------------------------
    # Header: includes + forward declarations
    # ------------------------------------------------------------------

    def _emit_header(self, funcs):
        self._w('/* Generated by emit_c_stmt.py — Sprint 20 */')
        self._w('#include "snobol4.h"')
        self._w('#include "mock_includes.h"')
        self._w('#include <stdio.h>')
        self._w('')
        # Forward-declare each user function
        for fi in funcs:
            safe = _safe_c_name(fi.name)
            self._w(f'static SnoVal uf_{safe}(SnoVal *_args, int _nargs);')
        self._w('')

    # ------------------------------------------------------------------
    # Main program body
    # ------------------------------------------------------------------

    def _emit_main_body_segments(self, segments):
        """Emit main program: interleaved segments (main stmts + residuals)."""
        self._func_suffix = '_MAIN'
        self._w('int program(void) {')
        self._w('    /* Jump to first statement */')
        # Find first stmt across all segments
        for offset, stmts in segments:
            if stmts:
                first_lbl = self._stmt_label(offset, stmts[0])
                self._w(f'    goto {first_lbl};')
                break
        self._w('')
        for offset, stmts in segments:
            self._emit_stmts(stmts, offset, is_main=True)

    def _emit_main_footer(self):
        self._emit_indirect_goto_stubs()
        self._w('/* --- program exit labels --- */')
        self._w('_PROG_END:')
        self._w('    return 0;')
        self._w('RETURN_LABEL_MAIN:')
        self._w('    return 0;')
        self._w('FRETURN_LABEL_MAIN:')
        self._w('    return 1;')
        self._w('NRETURN_LABEL_MAIN:')
        self._w('    return 0;')
        self._w('CONTINUE_LABEL_MAIN:')
        self._w('    return 0;')
        self._w('error:')
        self._w('ERROR_LABEL_MAIN:')
        self._w('    fprintf(stderr, "error label reached\\n");')
        self._w('    return 2;')
        self._w('err:')
        self._w('    fprintf(stderr, "err label reached\\n");')
        self._w('    return 2;')
        self._w('}')
        self._w('')

    # ------------------------------------------------------------------
    # Per-function C function
    # ------------------------------------------------------------------

    def _emit_function(self, fi):
        safe = _safe_c_name(fi.name)
        self._func_suffix = f'_{safe}'
        all_vars = fi.params + fi.locals_
        self._w(f'/* SNOBOL4 function: {fi.name}({", ".join(fi.params)}) locals={fi.locals_} */')
        self._w(f'static SnoVal uf_{safe}(SnoVal *_args, int _nargs) {{')
        # Save existing values of params+locals, bind params from args
        if all_vars:
            self._w(f'    /* Save and bind params/locals */')
            for v in all_vars:
                sv = _safe_c_name(v)
                self._w(f'    SnoVal _save_{sv} = var_get("{v}");')
            for i, p in enumerate(fi.params):
                self._w(f'    var_set("{p}", (_nargs > {i}) ? _args[{i}] : NULL_VAL);')
            for l in fi.locals_:
                self._w(f'    var_set("{l}", NULL_VAL);')
            self._w('')

        # Jump to entry label
        entry_c = _c_label(fi.entry_label)
        self._w(f'    goto {entry_c};')
        self._w('')
        self._w('')

        # Emit function body stmts
        self._emit_stmts(fi.stmts, fi.first_idx, is_main=False)

        # Return labels — per function
        ret_lbl  = f'RETURN_LABEL_{safe}'
        fret_lbl = f'FRETURN_LABEL_{safe}'
        nret_lbl = f'NRETURN_LABEL_{safe}'
        cont_lbl = f'CONTINUE_LABEL_{safe}'
        self._w(f'{ret_lbl}:')
        # RETURN: capture the function-name variable (the return value) BEFORE
        # restoring params/locals, then restore, then return it.
        # In SNOBOL4, :(RETURN) returns the current value of the function-name var.
        self._w(f'    {{')
        self._w(f'        SnoVal _retval = var_get("{fi.name}");')
        if all_vars:
            for v in all_vars:
                sv = _safe_c_name(v)
                self._w(f'        var_set("{v}", _save_{sv});')
        self._w(f'        return _retval;')
        self._w(f'    }}')
        self._w(f'{fret_lbl}:')
        if all_vars:
            for v in all_vars:
                sv = _safe_c_name(v)
                self._w(f'    var_set("{v}", _save_{sv});')
        self._w(f'    return FAIL_VAL;')
        self._w(f'{nret_lbl}:')
        if all_vars:
            for v in all_vars:
                sv = _safe_c_name(v)
                self._w(f'    var_set("{v}", _save_{sv});')
        self._w(f'    return NULL_VAL;  /* NRETURN */')
        self._w(f'{cont_lbl}:')
        if all_vars:
            for v in all_vars:
                sv = _safe_c_name(v)
                self._w(f'    var_set("{v}", _save_{sv});')
        self._w(f'    return NULL_VAL;  /* CONTINUE */')
        self._w(f'ERROR_LABEL_{safe}:')
        self._w(f'    fprintf(stderr, "** error in {fi.name}\\n");')
        if all_vars:
            for v in all_vars:
                sv = _safe_c_name(v)
                self._w(f'    var_set("{v}", _save_{sv});')
        self._w(f'    return FAIL_VAL;')
        self._emit_indirect_goto_stubs()
        self._w(f'}}')
        self._w('')

    # ------------------------------------------------------------------
    # main() + function registration
    # ------------------------------------------------------------------

    def _emit_main_c(self, funcs):
        self._w('int main(void) {')
        self._w('    runtime_init();')
        self._w('    inc_init();')
        for fi in funcs:
            safe = _safe_c_name(fi.name)
            spec = f'{fi.name}({",".join(fi.params)}){",".join(fi.locals_)}'
            self._w(f'    define("{spec}", uf_{safe});')
        self._w('    return program();')
        self._w('}')

    # ------------------------------------------------------------------
    # Statement list emitter (shared by main and functions)
    # ------------------------------------------------------------------

    def _emit_stmts(self, stmts, offset, is_main):
        n = len(stmts)
        for i, stmt in enumerate(stmts):
            abs_i = offset + i
            lbl = self._stmt_label(abs_i, stmt)
            next_lbl = (self._stmt_label(offset + i + 1, stmts[i+1])
                        if i+1 < n else ('_PROG_END' if is_main else
                                         f'RETURN_LABEL{self._func_suffix}'))

            self._w(f'{lbl}: {{  /* L{stmt.lineno} */')
            self._w(f'    comm_stno({stmt.lineno});')

            succ_lbl = next_lbl
            fail_lbl = next_lbl

            if stmt.goto:
                g = stmt.goto
                if g.unconditional:
                    cl = self._resolve_goto(g.unconditional)
                    succ_lbl = cl
                    fail_lbl = cl
                else:
                    if g.on_success:
                        succ_lbl = self._resolve_goto(g.on_success)
                    if g.on_failure:
                        fail_lbl = self._resolve_goto(g.on_failure)

            self._emit_stmt_body(stmt, succ_lbl, fail_lbl, next_lbl)
            self._w(f'}}')
            self._w()

    def _resolve_goto(self, target):
        """Resolve a SNOBOL4 goto target to a C label or indirect-goto stub label."""
        cl = _c_label(target)
        cl = self._map_special_label(cl, target)
        # If the target is not a defined label, it's an indirect (variable) goto.
        # Emit a stub C label that calls indirect_goto at runtime.
        if target.upper() not in self._defined_labels:
            stub = f'_sno_ind_{_safe_c_name(target)}'
            self._indirect_goto_stubs.add((stub, target))
            return stub
        return cl

    def _emit_indirect_goto_stubs(self):
        """Emit C label stubs for indirect (variable) gotos collected during emit."""
        if not self._indirect_goto_stubs:
            return
        is_main = (self._func_suffix == '_MAIN')
        ret_stmt = 'return 0;' if is_main else 'return NULL_VAL;'
        self._w('/* --- indirect goto stubs --- */')
        for stub, varname in sorted(self._indirect_goto_stubs):
            self._w(f'{stub}:')
            self._w(f'    indirect_goto("{varname}");')
            self._w(f'    {ret_stmt}')
        self._indirect_goto_stubs.clear()

    def _emit_goto_stmt(self, label):
        """Emit a C goto, handling indirect (variable) gotos."""
        if label.startswith('_INDIRECT_GOTO_'):
            varname = label[len('_INDIRECT_GOTO_'):]
            self._w(f'        indirect_goto("{varname}"); /* indirect goto :(varname) */')
            self._w(f'        return NULL_VAL; /* not reached */')
        else:
            self._w(f'        goto {label};')

    def _emit_stmt_body(self, stmt, succ_lbl, fail_lbl, next_lbl):
        subj  = stmt.subject
        pat   = stmt.pattern
        repl  = stmt.replacement

        # Case 1: pure function call (no subject, no pattern)
        if subj is not None and subj.kind == 'call' and pat is None and repl is None:
            name = subj.name.upper() if subj.name else ''
            ca = [emit_expr(a) for a in (subj.args or [])]
            if name == 'DEFINE':
                self._w(f'    define_spec({ca[0] if ca else "NULL_VAL"});')
            elif name == 'DATA':
                self._w(f'    data_define(to_str({ca[0] if ca else "NULL_VAL"}));')
            elif name == 'OPSYN':
                if len(ca) >= 3:
                    self._w(f'    opsyn({", ".join(ca)});')
                else:
                    self._w(f'    opsyn2({", ".join(ca)});')
            else:
                args_c = ', '.join(ca)
                n_args = len(ca)
                if n_args:
                    self._w(f'    {{')
                    self._w(f'        SnoVal _ret = aply("{subj.name}", (SnoVal[{n_args}]){{{args_c}}}, {n_args});')
                    self._w(f'        if (is_fail(_ret)) goto {fail_lbl};')
                    self._w(f'        goto {succ_lbl};')
                    self._w(f'    }}')
                    return
                else:
                    self._w(f'    {{')
                    self._w(f'        SnoVal _ret = aply("{subj.name}", NULL, 0);')
                    self._w(f'        if (is_fail(_ret)) goto {fail_lbl};')
                    self._w(f'        goto {succ_lbl};')
                    self._w(f'    }}')
                    return
            self._w(f'    goto {succ_lbl};')

        # Case 2: subject = replacement (assignment, no pattern)
        elif subj is not None and repl is not None and pat is None:
            rhs_c = emit_expr(repl)
            self._w(f'    {{')
            self._w(f'        SnoVal _rhs = {rhs_c};')
            self._w(f'        if (is_fail(_rhs)) goto {fail_lbl};')
            self._w(f'        {emit_assign_target(subj, "_rhs")[4:]}')
            self._w(f'        goto {succ_lbl};')
            self._w(f'    }}')

        # Case 3: subject pattern = replacement (pattern mtch + optional replacement)
        elif subj is not None and pat is not None:
            subj_c  = emit_expr(subj)
            pat_c   = emit_pattern_expr(pat)
            repl_c  = emit_expr(repl) if repl else None

            if repl_c:
                self._w(f'    {{')
                self._w(f'        SnoVal _subj = {subj_c};')
                self._w(f'        int _ok = match_and_replace(&_subj, {pat_c}, {repl_c});')
                self._w(f'        {emit_assign_target(subj, "_subj")[4:]}')
                self._w(f'        if (_ok) goto {succ_lbl};')
                self._w(f'        else goto {fail_lbl};')
                self._w(f'    }}')
            else:
                self._w(f'    {{')
                self._w(f'        SnoVal _subj = {subj_c};')
                self._w(f'        int _ok = match_pattern({pat_c}, to_str(_subj));')
                self._w(f'        if (_ok) goto {succ_lbl};')
                self._w(f'        else goto {fail_lbl};')
                self._w(f'    }}')

        # Case 3c: no subject but has pattern (predicate call sequence)
        # In SNOBOL4, "LT(i,n) Gen(nl)" with empty subject = mtch "" against the pattern
        # Predicate patterns (LT, GT, EQ etc.) succeed/fail purely on their predicate logic.
        elif subj is None and pat is not None and repl is None:
            pat_c = emit_pattern_expr(pat)
            self._w(f'    {{')
            self._w(f'        int _ok = match_pattern({pat_c}, "");')
            self._w(f'        if (_ok) goto {succ_lbl};')
            self._w(f'        else goto {fail_lbl};')
            self._w(f'    }}')

        # Case 3b: subject only, no pattern, no replacement (not a call)
        # Evaluate for side effects; always succeeds
        elif subj is not None and pat is None and repl is None:
            subj_c = emit_expr(subj)
            self._w(f'    {{')
            self._w(f'        SnoVal _sv = {subj_c};')
            self._w(f'        if (is_fail(_sv)) goto {fail_lbl};')
            self._w(f'        goto {succ_lbl};')
            self._w(f'    }}')

        # Case 4: bare goto only
        elif subj is None and pat is None and repl is None:
            self._w(f'    goto {succ_lbl};')

        # Case 5: OUTPUT = expr (no subject, has replacement)
        elif subj is None and repl is not None:
            rhs_c = emit_expr(repl)
            self._w(f'    {{')
            self._w(f'        SnoVal _rhs = {rhs_c};')
            self._w(f'        if (is_fail(_rhs)) goto {fail_lbl};')
            self._w(f'        output_val(_rhs);')
            self._w(f'        goto {succ_lbl};')
            self._w(f'    }}')

        else:
            self._w(f'    /* unhandled stmt shape */')
            self._w(f'    goto {next_lbl};')

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    def _map_special_label(self, cl, orig):
        upper = orig.upper()
        sfx = self._func_suffix  # e.g. '_findRefs' or '_MAIN'
        if upper == 'END':
            return '_PROG_END'
        if upper == 'RETURN':
            return f'RETURN_LABEL{sfx}'
        if upper == 'FRETURN':
            return f'FRETURN_LABEL{sfx}'
        if upper == 'NRETURN':
            return f'NRETURN_LABEL{sfx}'
        if upper == 'CONTINUE':
            return f'CONTINUE_LABEL{sfx}'
        # error/err — global error handler (goes to main program error label)
        if upper in ('ERROR', 'ERR'):
            return f'ERROR_LABEL{sfx}'
        return cl

    def _emit_error_label(self, sfx):
        """Emit a per-function error label that prints and fails."""
        self._w(f'ERROR_LABEL{sfx}:')
        self._w(f'    fprintf(stderr, "** program error\\n");')

    # Known builtin function names — these are real calls, not mislabeled stmts
    _KNOWN_BUILTINS = {
        'DEFINE','DATA','OPSYN','OUTPUT','INPUT','TERMINAL',
        'DIFFER','IDENT','EQ','NE','LT','LE','GT','GE',
        'LGT','LGE','LLT','LLE','LEQ','LNE',
        'APPLY','ARRAY','TABLE','SIZE','DUPL','REPLACE','SUBSTR',
        'REVERSE','LPAD','RPAD','TRIM','CHOP',
        'INTEGER','REAL','STRING','CONVERT','DATATYPE','PROTOTYPE',
        'EVAL','CODE','SORT','RSORT','COPY','COLLECT',
        'DATE','TIME','HOST','EXIT','ABORT',
        'TRACE','STOPTR','LOAD','UNLOAD',
        'SPAN','BREAK','BREAKX','ANY','NOTANY','ARBNO',
        'LEN','POS','RPOS','TAB','RTAB','REM',
        'ARB','BAL','FENCE','FAIL','SUCCEED',
        'SIN','COS','TAN','ATAN','EXP','LN','SQRT',
        'CHAR','NOTANY','FIELD','ARG','LOCAL',
        'ENDFILE','REWIND','BACKSPACE','EJECT',
        'SET','ITEM','REMDR',
        'PUSH','POP','TOP',           # inc functions
        'N','T','V','C',              # tree field accessors
        'BVISIT',                     # known user-defined
    }

    def _stmt_label(self, abs_i, stmt):
        """Return C label for this statement, handling parser mis-labeling."""
        if stmt.label:
            return _c_label(stmt.label)
        # Parser sometimes puts the SNOBOL4 label into subj.name
        # for:  label    (cond_expr)   :goto
        # Only synthesize if the name is an actual goto target AND not a
        # known function entry (those are real calls, not mislabeled stmts).
        subj = stmt.subject
        if (subj is not None and subj.kind == 'call' and subj.name and
                subj.name.upper() not in self._KNOWN_BUILTINS and
                stmt.pattern is None and stmt.replacement is None and
                subj.name.upper() in self._goto_targets and
                subj.name.upper() not in self._func_entry_lbls):
            return _c_label(subj.name)
        return f'_stmt_{abs_i}'

    def _w(self, line=''):
        self.lines.append(line)


def _safe_c_name(s):
    """Convert a SNOBOL4 identifier to a safe C identifier."""
    r = _re.sub(r'[^a-zA-Z0-9]', '_', s)
    if r and r[0].isdigit():
        r = 'f_' + r
    return r or 'anon'


def emit_program(prog: Program) -> strv:
    """Emit a complete C source file from a parsed SNOBOL4 program."""
    return StmtEmitter(prog).emit()


# -----------------------------------------------------------------------
# CLI
# -----------------------------------------------------------------------

if __name__ == '__main__':
    import sys
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'parser'))
    from parser import parse_file

    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('source')
    ap.add_argument('-I', dest='include_dirs', action='append', default=[])
    args = ap.parse_args()

    prog = parse_file(args.source, include_dirs=args.include_dirs)
    print(f'/* Parsed {len(prog.stmts)} statements */', file=sys.stderr)
    print(emit_program(prog))
