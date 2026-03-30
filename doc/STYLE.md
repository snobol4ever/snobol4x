# STYLE.md — scrip-cc Source Style Guide

Applies to all files under `src/`. Enforced at M-G7-STYLE-*.

---

## Indentation and braces

- **4 spaces** per indent level. No tabs anywhere.
- Opening brace on the same line as the control statement (`K&R` style):
  ```c
  if (cond) {
      ...
  }
  ```
- Single-statement `if`/`for`/`while` bodies use braces anyway when the body
  is non-trivial. Trivial one-liners may omit braces (`if (!x) return;`).
- `switch` cases are not indented relative to the `switch`. `case` bodies are
  indented 4 spaces from the `case` keyword.

---

## Function header block

Every non-trivial static function gets a banner comment:

```c
/* function_name — one-line purpose.
 * Longer description if needed.
 * Stack contract: [before] → [after]  (for JVM emitters)
 * Locals: describe any scratch locals used. */
static ReturnType function_name(args) {
```

Trivial helpers (1–3 lines) may use a one-liner: `/* purpose */`.

---

## Generated-code column widths (backend emitters)

Three-column layout for all emitted assembly/bytecode lines:

| Column | Constant | Value | Content |
|--------|----------|-------|---------|
| col1   | `COL_W`  | 28    | label (left-justified, padded to COL_W) |
| col2   | `COL2_W` | 12    | opcode/macro name |
| col3   | —        | COL_W + COL2_W + 1 | operands |
| comment | `COL_CMT` | 72  | inline comment (`; …`) |

`COL_CMT = COL_W + COL2_W + 32 = 72`

These values are `#define`d at the top of each backend emitter file and must
not be changed without updating all baselines.

**JVM emitters** use a simpler two-field layout (label + instruction/operand
freeform) because Jasmin does not require fixed columns. Column constants are
still defined for label padding.

---

## Naming conventions

| Kind | Convention | Example |
|------|-----------|---------|
| Functions | `snake_case` | `jvm_emit_expr` |
| Static file-scope vars | `snake_case` with `jvm_`/`x64_`/`net_` prefix | `jvm_need_input_helper` |
| IR enum values | `E_` prefix, `UPPER_SNAKE` | `E_FLIT`, `E_OPSYN` |
| Statement IR kinds | `STMT_` prefix | `STMT_t` |
| Emitter-local labels | `L<prefix>_<uid>` | `Larf_0`, `Ldiff_f_1` |
| Runtime helpers (JVM) | `sno_<name>` | `sno_fmt_double`, `sno_indr_get` |

Backend-specific prefixes:
- JVM: `jvm_`
- x86/x64: `x64_` (files stay `emit_x64.c`; human name is **x86**)
- .NET: `net_`
- C: `c_` (dead backend — do not modify)

---

## Comments

- Block comments: `/* ... */` (C89 style). No `//` line comments in `.c` files.
- Section dividers use `/* ── Section name ─── */` (em-dash padding to ~78 chars).
- Generated Jasmin lines use `; comment` (semicolon, Jasmin convention).
- Do not comment obvious code. Comment the *why*, not the *what*.
- Every `case E_*:` in a switch emitter gets a one-line comment naming the node.

---

## File header

Every `.c` and `.h` file begins with:

```c
/*
 * filename.c — one-line description
 *
 * [Optional multi-line context — pipeline, sprint map, design notes.]
 */
```

No copyright block. No author line (commits carry authorship).

---

## Includes

- System headers first (`<stdio.h>`, `<stdlib.h>`, `<string.h>`), alphabetical.
- Project headers next (`"scrip_cc.h"`, `"ir/ir.h"`, …), alphabetical.
- One blank line between system and project groups.

---

## Misc

- Line length: soft limit 100 chars, hard limit 120. Emitted-string literals
  may exceed this when they mirror fixed Jasmin/NASM syntax.
- No trailing whitespace.
- Files end with a single newline.
- `static` on all file-scope helpers — no leaking internal symbols.
- `const` on pointer parameters where the callee does not mutate.

---

*STYLE.md is the authoritative reference. When in doubt: match the existing
style in `emit_x64.c` (the oldest and most consistent backend).*
