# Session 61 artifact

## Changes since session 60
- Three-column pretty layout: all emit functions use PLG/PL/PS/PG macros
- Generated C now has label col (0-17), stmt col (18-59), goto col (60+)
- 25,377 lines (vs 27,540 before — 2,163 shorter, denser layout)
- Greek port labels α/β/γ/ω throughout (Lon's watermark)

## Stats
- Lines: 25377
- MD5: 98d622a449e76c487284ad1fb1db8b0a
- GCC: 0 errors

## Active bug
Parse Error for real statements. pat_Stmt fails — root cause is ~ (tilde/tree-tag)
operator compiling incorrectly. pat_Label = BREAK(...) ~ 'Label' — the ~ should
allow optional/conditional matching but currently acts as strict CAT, so unlabeled
statements always fail pat_Label → pat_Stmt → Parse Error.
