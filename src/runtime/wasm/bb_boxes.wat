;; bb_boxes.wat — Dynamic Byrd Box Runtime for WebAssembly
;;
;; Direct port of src/runtime/boxes/bb_*.c
;; One function pair per box: $bb_NAME_a (α port) and $bb_NAME_b (β port).
;;
;; Memory layout for box state (within the match-state arena):
;;   Each box factory writes its parameters into a fixed-size state struct
;;   in the arena, returns a box_handle (i32 index into arena).
;;   Box functions receive the box_handle and read their state from the arena.
;;
;; Global match state (mirrors C globals Σ/Δ/Ω in stmt_exec.c):
;;   $Σ   — subject string pointer (into linear memory)
;;   $Δ   — cursor (mutable i32)
;;   $Ω   — subject length (i32)
;;
;; Port protocol:
;;   α(box_handle) → i32   — fresh entry: returns matched len ≥0, or -1 (ω/fail)
;;   β(box_handle) → i32   — backtrack: returns matched len ≥0, or -1 (ω/fail)
;;   A non-negative return means γ fired; -1 means ω fired.
;;   The caller (scan loop) updates $Δ by adding the returned length.
;;   (For boxes that set $Δ internally, return value is advisory.)
;;
;; Arena layout (BOX_ARENA_BASE = 0x50000, 64KB):
;;   Each box entry: 32 bytes (padded).
;;   Offset 0:  box type tag (i32)
;;   Offset 4:  param0 (i32) — lit ptr / n / char ptr / etc.
;;   Offset 8:  param1 (i32) — lit len / child_a handle / etc.
;;   Offset 12: param2 (i32) — child_b handle / flags
;;   Offset 16: state0 (i32) — mutable: cursor saves, count, depth, etc.
;;   Offset 20: state1 (i32)
;;   Offset 24: state2 (i32)
;;   Offset 28: state3 (i32)
;;
;; Author: Claude Sonnet 4.6 — SJ-5, 2026-04-02
;; Oracle: src/runtime/boxes/bb_*.c + bb_box.h
;; Side-by-side companion: src/runtime/js/bb_boxes.js

(module

  ;; ── Memory ──────────────────────────────────────────────────────────────
  (memory (export "memory") 8)

  ;; ── Constants ───────────────────────────────────────────────────────────
  ;; BOX_ARENA_BASE = 0x50000 (320KB)
  ;; STACK_BASE     = 0x60000 (384KB) — ARBNO stack frames
  ;; CAPTURE_BASE   = 0x70000 (448KB) — pending capture list

  ;; ── Global match state (Σ/Δ/Ω — set by exec_stmt Phase 1) ───────────────
  (global $Σ   (mut i32) (i32.const 0))   ;; subject string base pointer
  (global $Δ   (mut i32) (i32.const 0))   ;; cursor
  (global $Ω   (mut i32) (i32.const 0))   ;; subject length
  (global $arena_ptr (mut i32) (i32.const 0x50000))
  (global $abort_flag (mut i32) (i32.const 0))

  ;; Pending captures: count at CAPTURE_BASE; entries at CAPTURE_BASE+4
  ;; Each entry: varname_ptr (i32) + value_ptr (i32) + value_len (i32) = 12 bytes

  ;; ── bb_set_subject — Phase 1 init ────────────────────────────────────────
  (func $bb_set_subject (export "bb_set_subject") (param $ptr i32) (param $len i32)
    (global.set $Σ (local.get $ptr))
    (global.set $Δ (i32.const 0))
    (global.set $Ω (local.get $len))
    ;; reset arena
    (global.set $arena_ptr (i32.const 0x50000))
    ;; reset pending captures
    (i32.store (i32.const 0x70000) (i32.const 0))
    (global.set $abort_flag (i32.const 0))
  )

  ;; ── arena allocator — 32 bytes per box ───────────────────────────────────
  (func $arena_alloc (export "arena_alloc") (result i32)
    (local $h i32)
    (local.set $h (global.get $arena_ptr))
    (global.set $arena_ptr (i32.add (local.get $h) (i32.const 32)))
    (local.get $h)
  )

  ;; ── field accessors (offsets into 32-byte box slot) ──────────────────────
  ;; param0 @ +4, param1 @ +8, param2 @ +12
  ;; state0 @ +16, state1 @ +20, state2 @ +24, state3 @ +28

  (func $p0 (param $h i32) (result i32)
    (i32.load (i32.add (local.get $h) (i32.const 4))))
  (func $p1 (param $h i32) (result i32)
    (i32.load (i32.add (local.get $h) (i32.const 8))))
  (func $p2 (param $h i32) (result i32)
    (i32.load (i32.add (local.get $h) (i32.const 12))))
  (func $s0 (param $h i32) (result i32)
    (i32.load (i32.add (local.get $h) (i32.const 16))))
  (func $s1 (param $h i32) (result i32)
    (i32.load (i32.add (local.get $h) (i32.const 20))))
  (func $set_s0 (param $h i32) (param $v i32)
    (i32.store (i32.add (local.get $h) (i32.const 16)) (local.get $v)))
  (func $set_s1 (param $h i32) (param $v i32)
    (i32.store (i32.add (local.get $h) (i32.const 20)) (local.get $v)))
  (func $set_s2 (param $h i32) (param $v i32)
    (i32.store (i32.add (local.get $h) (i32.const 24)) (local.get $v)))
  (func $s2 (param $h i32) (result i32)
    (i32.load (i32.add (local.get $h) (i32.const 24))))

  ;; ── memcmp helper — compare n bytes at ptr_a and ptr_b ───────────────────
  (func $memcmp (param $a i32) (param $b i32) (param $n i32) (result i32)
    (local $i i32)
    (local.set $i (i32.const 0))
    (block $done
      (loop $cmp
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (br_if $done
          (i32.ne
            (i32.load8_u (i32.add (local.get $a) (local.get $i)))
            (i32.load8_u (i32.add (local.get $b) (local.get $i)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $cmp)))
    (i32.eq (local.get $i) (local.get $n))  ;; 1=equal, 0=differ
  )

  ;; ── strchr helper — find char c in string at ptr (0-terminated scan) ─────
  ;; Returns 1 if found, 0 if not; scans up to max_len chars.
  (func $strchr (param $ptr i32) (param $c i32) (result i32)
    (local $i i32)
    (local $ch i32)
    (local.set $i (i32.const 0))
    (block $found
      (block $notfound
        (loop $scan
          (local.set $ch (i32.load8_u (i32.add (local.get $ptr) (local.get $i))))
          (br_if $notfound (i32.eqz (local.get $ch)))                  ;; NUL = end
          (br_if $found (i32.eq (local.get $ch) (local.get $c)))       ;; match
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $scan))
        (return (i32.const 0)))                                         ;; not found
      (return (i32.const 1)))                                           ;; found
    (i32.const 0)
  )

  ;; ══════════════════════════════════════════════════════════════════════════
  ;; bb_lit — literal string match
  ;; Oracle: bb_lit.c
  ;; State: p0=lit_ptr, p1=lit_len
  ;; α: if Δ+len>Ω → -1; if memcmp(Σ+Δ, lit, len) != 0 → -1; Δ+=len; return len
  ;; β: Δ-=len; return -1
  ;; ══════════════════════════════════════════════════════════════════════════
  (func $bb_lit_new (export "bb_lit_new") (param $lit_ptr i32) (param $lit_len i32) (result i32)
    (local $h i32)
    (local.set $h (call $arena_alloc))
    (i32.store (i32.add (local.get $h) (i32.const 4)) (local.get $lit_ptr))
    (i32.store (i32.add (local.get $h) (i32.const 8)) (local.get $lit_len))
    (local.get $h)
  )
  (func $bb_lit_a (export "bb_lit_a") (param $h i32) (result i32)
    (local $len i32)
    (local.set $len (call $p1 (local.get $h)))
    (if (i32.gt_u (i32.add (global.get $Δ) (local.get $len)) (global.get $Ω))
      (then (return (i32.const -1))))
    (if (i32.eqz (call $memcmp
          (i32.add (global.get $Σ) (global.get $Δ))
          (call $p0 (local.get $h))
          (local.get $len)))
      (then (return (i32.const -1))))
    (global.set $Δ (i32.add (global.get $Δ) (local.get $len)))
    (local.get $len)
  )
  (func $bb_lit_b (export "bb_lit_b") (param $h i32) (result i32)
    (global.set $Δ (i32.sub (global.get $Δ) (call $p1 (local.get $h))))
    (i32.const -1)
  )

  ;; ══════════════════════════════════════════════════════════════════════════
  ;; bb_any — match one char if in charset
  ;; Oracle: bb_any.c
  ;; State: p0=chars_ptr
  ;; α: if Δ>=Ω or Σ[Δ] not in chars → -1; Δ++; return 1
  ;; β: Δ--; return -1
  ;; ══════════════════════════════════════════════════════════════════════════
  (func $bb_any_new (export "bb_any_new") (param $chars_ptr i32) (result i32)
    (local $h i32)
    (local.set $h (call $arena_alloc))
    (i32.store (i32.add (local.get $h) (i32.const 4)) (local.get $chars_ptr))
    (local.get $h)
  )
  (func $bb_any_a (export "bb_any_a") (param $h i32) (result i32)
    (if (i32.ge_u (global.get $Δ) (global.get $Ω)) (then (return (i32.const -1))))
    (if (i32.eqz (call $strchr
          (call $p0 (local.get $h))
          (i32.load8_u (i32.add (global.get $Σ) (global.get $Δ)))))
      (then (return (i32.const -1))))
    (global.set $Δ (i32.add (global.get $Δ) (i32.const 1)))
    (i32.const 1)
  )
  (func $bb_any_b (export "bb_any_b") (param $h i32) (result i32)
    (global.set $Δ (i32.sub (global.get $Δ) (i32.const 1)))
    (i32.const -1)
  )

  ;; ══════════════════════════════════════════════════════════════════════════
  ;; bb_notany — match one char NOT in charset
  ;; Oracle: bb_notany.c
  ;; α: if Δ>=Ω or Σ[Δ] in chars → -1; Δ++; return 1
  ;; β: Δ--; return -1
  ;; ══════════════════════════════════════════════════════════════════════════
  (func $bb_notany_new (export "bb_notany_new") (param $chars_ptr i32) (result i32)
    (local $h i32)
    (local.set $h (call $arena_alloc))
    (i32.store (i32.add (local.get $h) (i32.const 4)) (local.get $chars_ptr))
    (local.get $h)
  )
  (func $bb_notany_a (export "bb_notany_a") (param $h i32) (result i32)
    (if (i32.ge_u (global.get $Δ) (global.get $Ω)) (then (return (i32.const -1))))
    (if (call $strchr
          (call $p0 (local.get $h))
          (i32.load8_u (i32.add (global.get $Σ) (global.get $Δ))))
      (then (return (i32.const -1))))
    (global.set $Δ (i32.add (global.get $Δ) (i32.const 1)))
    (i32.const 1)
  )
  (func $bb_notany_b (export "bb_notany_b") (param $h i32) (result i32)
    (global.set $Δ (i32.sub (global.get $Δ) (i32.const 1)))
    (i32.const -1)
  )

  ;; ══════════════════════════════════════════════════════════════════════════
  ;; bb_len — match exactly n characters
  ;; Oracle: bb_len.c
  ;; State: p0=n
  ;; α: if Δ+n>Ω → -1; Δ+=n; return n
  ;; β: Δ-=n; return -1
  ;; ══════════════════════════════════════════════════════════════════════════
  (func $bb_len_new (export "bb_len_new") (param $n i32) (result i32)
    (local $h i32)
    (local.set $h (call $arena_alloc))
    (i32.store (i32.add (local.get $h) (i32.const 4)) (local.get $n))
    (local.get $h)
  )
  (func $bb_len_a (export "bb_len_a") (param $h i32) (result i32)
    (local $n i32)
    (local.set $n (call $p0 (local.get $h)))
    (if (i32.gt_u (i32.add (global.get $Δ) (local.get $n)) (global.get $Ω))
      (then (return (i32.const -1))))
    (global.set $Δ (i32.add (global.get $Δ) (local.get $n)))
    (local.get $n)
  )
  (func $bb_len_b (export "bb_len_b") (param $h i32) (result i32)
    (global.set $Δ (i32.sub (global.get $Δ) (call $p0 (local.get $h))))
    (i32.const -1)
  )

  ;; ══════════════════════════════════════════════════════════════════════════
  ;; bb_pos — assert cursor == n (zero-width)
  ;; Oracle: bb_pos.c
  ;; α: if Δ != n → -1; return 0
  ;; β: return -1
  ;; ══════════════════════════════════════════════════════════════════════════
  (func $bb_pos_new (export "bb_pos_new") (param $n i32) (result i32)
    (local $h i32)
    (local.set $h (call $arena_alloc))
    (i32.store (i32.add (local.get $h) (i32.const 4)) (local.get $n))
    (local.get $h)
  )
  (func $bb_pos_a (export "bb_pos_a") (param $h i32) (result i32)
    (if (i32.ne (global.get $Δ) (call $p0 (local.get $h)))
      (then (return (i32.const -1))))
    (i32.const 0)
  )
  (func $bb_pos_b (export "bb_pos_b") (param $h i32) (result i32)
    (i32.const -1)
  )

  ;; ══════════════════════════════════════════════════════════════════════════
  ;; bb_rpos — assert cursor == Ω-n (zero-width)
  ;; Oracle: bb_rpos.c
  ;; ══════════════════════════════════════════════════════════════════════════
  (func $bb_rpos_new (export "bb_rpos_new") (param $n i32) (result i32)
    (local $h i32)
    (local.set $h (call $arena_alloc))
    (i32.store (i32.add (local.get $h) (i32.const 4)) (local.get $n))
    (local.get $h)
  )
  (func $bb_rpos_a (export "bb_rpos_a") (param $h i32) (result i32)
    (if (i32.ne (global.get $Δ) (i32.sub (global.get $Ω) (call $p0 (local.get $h))))
      (then (return (i32.const -1))))
    (i32.const 0)
  )
  (func $bb_rpos_b (export "bb_rpos_b") (param $h i32) (result i32)
    (i32.const -1)
  )

  ;; ══════════════════════════════════════════════════════════════════════════
  ;; bb_tab — advance cursor TO absolute position n
  ;; Oracle: bb_tab.c
  ;; State: p0=n, s0=saved advance
  ;; α: if Δ>n → -1; advance=n-Δ; Δ=n; return advance
  ;; β: Δ-=advance; return -1
  ;; ══════════════════════════════════════════════════════════════════════════
  (func $bb_tab_new (export "bb_tab_new") (param $n i32) (result i32)
    (local $h i32)
    (local.set $h (call $arena_alloc))
    (i32.store (i32.add (local.get $h) (i32.const 4)) (local.get $n))
    (local.get $h)
  )
  (func $bb_tab_a (export "bb_tab_a") (param $h i32) (result i32)
    (local $n i32) (local $adv i32)
    (local.set $n (call $p0 (local.get $h)))
    (if (i32.gt_u (global.get $Δ) (local.get $n)) (then (return (i32.const -1))))
    (local.set $adv (i32.sub (local.get $n) (global.get $Δ)))
    (call $set_s0 (local.get $h) (local.get $adv))
    (global.set $Δ (local.get $n))
    (local.get $adv)
  )
  (func $bb_tab_b (export "bb_tab_b") (param $h i32) (result i32)
    (global.set $Δ (i32.sub (global.get $Δ) (call $s0 (local.get $h))))
    (i32.const -1)
  )

  ;; ══════════════════════════════════════════════════════════════════════════
  ;; bb_rtab — advance cursor TO Ω-n
  ;; Oracle: bb_rtab.c
  ;; ══════════════════════════════════════════════════════════════════════════
  (func $bb_rtab_new (export "bb_rtab_new") (param $n i32) (result i32)
    (local $h i32)
    (local.set $h (call $arena_alloc))
    (i32.store (i32.add (local.get $h) (i32.const 4)) (local.get $n))
    (local.get $h)
  )
  (func $bb_rtab_a (export "bb_rtab_a") (param $h i32) (result i32)
    (local $target i32) (local $adv i32)
    (local.set $target (i32.sub (global.get $Ω) (call $p0 (local.get $h))))
    (if (i32.gt_u (global.get $Δ) (local.get $target)) (then (return (i32.const -1))))
    (local.set $adv (i32.sub (local.get $target) (global.get $Δ)))
    (call $set_s0 (local.get $h) (local.get $adv))
    (global.set $Δ (local.get $target))
    (local.get $adv)
  )
  (func $bb_rtab_b (export "bb_rtab_b") (param $h i32) (result i32)
    (global.set $Δ (i32.sub (global.get $Δ) (call $s0 (local.get $h))))
    (i32.const -1)
  )

  ;; ══════════════════════════════════════════════════════════════════════════
  ;; bb_rem — match entire remainder; no backtrack
  ;; Oracle: bb_rem.c
  ;; α: advance = Ω-Δ; Δ=Ω; return advance
  ;; β: return -1
  ;; ══════════════════════════════════════════════════════════════════════════
  (func $bb_rem_new (export "bb_rem_new") (result i32)
    (call $arena_alloc)
  )
  (func $bb_rem_a (export "bb_rem_a") (param $h i32) (result i32)
    (local $adv i32)
    (local.set $adv (i32.sub (global.get $Ω) (global.get $Δ)))
    (global.set $Δ (global.get $Ω))
    (local.get $adv)
  )
  (func $bb_rem_b (export "bb_rem_b") (param $h i32) (result i32)
    (i32.const -1)
  )

  ;; ══════════════════════════════════════════════════════════════════════════
  ;; bb_span — longest prefix ≥1 char in charset; β ω
  ;; Oracle: bb_span.c
  ;; State: p0=chars_ptr, s0=saved δ
  ;; ══════════════════════════════════════════════════════════════════════════
  (func $bb_span_new (export "bb_span_new") (param $chars_ptr i32) (result i32)
    (local $h i32)
    (local.set $h (call $arena_alloc))
    (i32.store (i32.add (local.get $h) (i32.const 4)) (local.get $chars_ptr))
    (local.get $h)
  )
  (func $bb_span_a (export "bb_span_a") (param $h i32) (result i32)
    (local $d i32)
    (local.set $d (i32.const 0))
    (block $done
      (loop $scan
        (br_if $done (i32.ge_u (i32.add (global.get $Δ) (local.get $d)) (global.get $Ω)))
        (br_if $done (i32.eqz (call $strchr
            (call $p0 (local.get $h))
            (i32.load8_u (i32.add (i32.add (global.get $Σ) (global.get $Δ)) (local.get $d))))))
        (local.set $d (i32.add (local.get $d) (i32.const 1)))
        (br $scan)))
    (if (i32.le_s (local.get $d) (i32.const 0)) (then (return (i32.const -1))))
    (call $set_s0 (local.get $h) (local.get $d))
    (global.set $Δ (i32.add (global.get $Δ) (local.get $d)))
    (local.get $d)
  )
  (func $bb_span_b (export "bb_span_b") (param $h i32) (result i32)
    (global.set $Δ (i32.sub (global.get $Δ) (call $s0 (local.get $h))))
    (i32.const -1)
  )

  ;; ══════════════════════════════════════════════════════════════════════════
  ;; bb_brk (BREAK) — scan to first char IN set; may be zero-width
  ;; Oracle: bb_brk.c
  ;; ══════════════════════════════════════════════════════════════════════════
  (func $bb_brk_new (export "bb_brk_new") (param $chars_ptr i32) (result i32)
    (local $h i32)
    (local.set $h (call $arena_alloc))
    (i32.store (i32.add (local.get $h) (i32.const 4)) (local.get $chars_ptr))
    (local.get $h)
  )
  (func $bb_brk_a (export "bb_brk_a") (param $h i32) (result i32)
    (local $d i32)
    (local.set $d (i32.const 0))
    (block $done
      (loop $scan
        (br_if $done (i32.ge_u (i32.add (global.get $Δ) (local.get $d)) (global.get $Ω)))
        (br_if $done (call $strchr
            (call $p0 (local.get $h))
            (i32.load8_u (i32.add (i32.add (global.get $Σ) (global.get $Δ)) (local.get $d)))))
        (local.set $d (i32.add (local.get $d) (i32.const 1)))
        (br $scan)))
    (if (i32.ge_u (i32.add (global.get $Δ) (local.get $d)) (global.get $Ω))
      (then (return (i32.const -1))))
    (call $set_s0 (local.get $h) (local.get $d))
    (global.set $Δ (i32.add (global.get $Δ) (local.get $d)))
    (local.get $d)
  )
  (func $bb_brk_b (export "bb_brk_b") (param $h i32) (result i32)
    (global.set $Δ (i32.sub (global.get $Δ) (call $s0 (local.get $h))))
    (i32.const -1)
  )

  ;; ══════════════════════════════════════════════════════════════════════════
  ;; bb_breakx (BREAKX) — like BRK but fails on zero advance
  ;; Oracle: bb_breakx.c
  ;; ══════════════════════════════════════════════════════════════════════════
  (func $bb_breakx_new (export "bb_breakx_new") (param $chars_ptr i32) (result i32)
    (local $h i32)
    (local.set $h (call $arena_alloc))
    (i32.store (i32.add (local.get $h) (i32.const 4)) (local.get $chars_ptr))
    (local.get $h)
  )
  (func $bb_breakx_a (export "bb_breakx_a") (param $h i32) (result i32)
    (local $d i32)
    (local.set $d (i32.const 0))
    (block $done
      (loop $scan
        (br_if $done (i32.ge_u (i32.add (global.get $Δ) (local.get $d)) (global.get $Ω)))
        (br_if $done (call $strchr
            (call $p0 (local.get $h))
            (i32.load8_u (i32.add (i32.add (global.get $Σ) (global.get $Δ)) (local.get $d)))))
        (local.set $d (i32.add (local.get $d) (i32.const 1)))
        (br $scan)))
    ;; fail if zero advance or reached end
    (if (i32.or (i32.eqz (local.get $d))
                (i32.ge_u (i32.add (global.get $Δ) (local.get $d)) (global.get $Ω)))
      (then (return (i32.const -1))))
    (call $set_s0 (local.get $h) (local.get $d))
    (global.set $Δ (i32.add (global.get $Δ) (local.get $d)))
    (local.get $d)
  )
  (func $bb_breakx_b (export "bb_breakx_b") (param $h i32) (result i32)
    (global.set $Δ (i32.sub (global.get $Δ) (call $s0 (local.get $h))))
    (i32.const -1)
  )

  ;; ══════════════════════════════════════════════════════════════════════════
  ;; bb_arb — match 0..n chars lazily; β extends by 1
  ;; Oracle: bb_arb.c
  ;; State: s0=count, s1=start
  ;; α: count=0; start=Δ; return 0 (zero-width γ)
  ;; β: count++; if start+count>Ω → -1; Δ=start+count; return count
  ;; ══════════════════════════════════════════════════════════════════════════
  (func $bb_arb_new (export "bb_arb_new") (result i32)
    (call $arena_alloc)
  )
  (func $bb_arb_a (export "bb_arb_a") (param $h i32) (result i32)
    (call $set_s0 (local.get $h) (i32.const 0))
    (call $set_s1 (local.get $h) (global.get $Δ))
    (i32.const 0)
  )
  (func $bb_arb_b (export "bb_arb_b") (param $h i32) (result i32)
    (local $count i32) (local $start i32)
    (local.set $count (i32.add (call $s0 (local.get $h)) (i32.const 1)))
    (local.set $start (call $s1 (local.get $h)))
    (call $set_s0 (local.get $h) (local.get $count))
    (if (i32.gt_u (i32.add (local.get $start) (local.get $count)) (global.get $Ω))
      (then (return (i32.const -1))))
    (global.set $Δ (i32.add (local.get $start) (local.get $count)))
    (local.get $count)
  )

  ;; ══════════════════════════════════════════════════════════════════════════
  ;; bb_fence — succeed once; β cuts
  ;; Oracle: bb_fence.c
  ;; α: return 0 (zero-width γ)
  ;; β: return -1 (cut)
  ;; ══════════════════════════════════════════════════════════════════════════
  (func $bb_fence_new (export "bb_fence_new") (result i32)
    (call $arena_alloc)
  )
  (func $bb_fence_a (export "bb_fence_a") (param $h i32) (result i32)
    (i32.const 0)
  )
  (func $bb_fence_b (export "bb_fence_b") (param $h i32) (result i32)
    (i32.const -1)
  )

  ;; ══════════════════════════════════════════════════════════════════════════
  ;; bb_fail — always ω
  ;; Oracle: bb_fail.c
  ;; ══════════════════════════════════════════════════════════════════════════
  (func $bb_fail_new (export "bb_fail_new") (result i32)
    (call $arena_alloc)
  )
  (func $bb_fail_a (export "bb_fail_a") (param $h i32) (result i32)
    (i32.const -1)
  )
  (func $bb_fail_b (export "bb_fail_b") (param $h i32) (result i32)
    (i32.const -1)
  )

  ;; ══════════════════════════════════════════════════════════════════════════
  ;; bb_succeed — always γ zero-width; outer scan loop retries position
  ;; Oracle: bb_succeed.c
  ;; ══════════════════════════════════════════════════════════════════════════
  (func $bb_succeed_new (export "bb_succeed_new") (result i32)
    (call $arena_alloc)
  )
  (func $bb_succeed_a (export "bb_succeed_a") (param $h i32) (result i32)
    (i32.const 0)
  )
  (func $bb_succeed_b (export "bb_succeed_b") (param $h i32) (result i32)
    (i32.const 0)   ;; succeed on β too — outer scan advances
  )

  ;; ══════════════════════════════════════════════════════════════════════════
  ;; bb_abort — always ω; set abort_flag for exec_stmt to detect
  ;; Oracle: bb_abort.c
  ;; ══════════════════════════════════════════════════════════════════════════
  (func $bb_abort_new (export "bb_abort_new") (result i32)
    (call $arena_alloc)
  )
  (func $bb_abort_a (export "bb_abort_a") (param $h i32) (result i32)
    (global.set $abort_flag (i32.const 1))
    (i32.const -1)
  )
  (func $bb_abort_b (export "bb_abort_b") (param $h i32) (result i32)
    (global.set $abort_flag (i32.const 1))
    (i32.const -1)
  )
  (func $bb_aborted (export "bb_aborted") (result i32)
    (global.get $abort_flag)
  )

  ;; ══════════════════════════════════════════════════════════════════════════
  ;; bb_eps — zero-width success once; done flag prevents double-γ
  ;; Oracle: bb_eps.c
  ;; State: s0=done
  ;; ══════════════════════════════════════════════════════════════════════════
  (func $bb_eps_new (export "bb_eps_new") (result i32)
    (local $h i32)
    (local.set $h (call $arena_alloc))
    (call $set_s0 (local.get $h) (i32.const 0))
    (local.get $h)
  )
  (func $bb_eps_a (export "bb_eps_a") (param $h i32) (result i32)
    (if (call $s0 (local.get $h)) (then (return (i32.const -1))))
    (call $set_s0 (local.get $h) (i32.const 1))
    (i32.const 0)
  )
  (func $bb_eps_b (export "bb_eps_b") (param $h i32) (result i32)
    (i32.const -1)
  )

  ;; ══════════════════════════════════════════════════════════════════════════
  ;; bb_not — \X: succeed iff X fails; β always ω
  ;; Oracle: bb_not.c
  ;; State: p0=child_handle, s0=saved Δ
  ;; α: save Δ; run child.α; restore Δ; if child γ → -1; if child ω → 0
  ;; β: -1
  ;; Note: composite boxes (seq/alt/arbno/not/interr) store child handles
  ;;       in p0/p1; the scan loop dispatches by tag at p-2 (not shown here
  ;;       for clarity — full dispatcher in sno_engine.wat).
  ;; ══════════════════════════════════════════════════════════════════════════
  ;; (NOT, INTERR, CAPTURE, ATP, DVAR, SEQ, ALT, ARBNO are composite — their
  ;;  α/β implementations depend on the engine dispatch table in sno_engine.wat.
  ;;  Stubs are exported here; full implementations live in sno_engine.wat where
  ;;  the function-table dispatch is available.)

  (func $bb_not_new (export "bb_not_new") (param $child_h i32) (result i32)
    (local $h i32)
    (local.set $h (call $arena_alloc))
    (i32.store (i32.add (local.get $h) (i32.const 4)) (local.get $child_h))
    (local.get $h)
  )
  ;; bb_not_a / bb_not_b — implemented in sno_engine.wat (need dispatch table)

  (func $bb_interr_new (export "bb_interr_new") (param $child_h i32) (result i32)
    (local $h i32)
    (local.set $h (call $arena_alloc))
    (i32.store (i32.add (local.get $h) (i32.const 4)) (local.get $child_h))
    (local.get $h)
  )

  ;; ══════════════════════════════════════════════════════════════════════════
  ;; bb_atp — @var: write cursor position; no backtrack
  ;; Oracle: bb_atp.c
  ;; State: p0=varname_ptr
  ;; α: write Δ to capture list as ATP entry; return 0
  ;; β: -1
  ;; ══════════════════════════════════════════════════════════════════════════
  (func $bb_atp_new (export "bb_atp_new") (param $varname_ptr i32) (result i32)
    (local $h i32)
    (local.set $h (call $arena_alloc))
    (i32.store (i32.add (local.get $h) (i32.const 4)) (local.get $varname_ptr))
    (local.get $h)
  )
  (func $bb_atp_a (export "bb_atp_a") (param $h i32) (result i32)
    ;; Write capture entry: type=ATP(2), varname_ptr, cursor value
    (local $cap_base i32) (local $cnt i32)
    (local.set $cnt (i32.load (i32.const 0x70000)))
    (local.set $cap_base (i32.add (i32.const 0x70004)
                                  (i32.mul (local.get $cnt) (i32.const 12))))
    (i32.store (local.get $cap_base) (i32.const 2))                      ;; type=ATP
    (i32.store (i32.add (local.get $cap_base) (i32.const 4)) (call $p0 (local.get $h)))  ;; varname_ptr
    (i32.store (i32.add (local.get $cap_base) (i32.const 8)) (global.get $Δ))            ;; cursor value
    (i32.store (i32.const 0x70000) (i32.add (local.get $cnt) (i32.const 1)))
    (i32.const 0)
  )
  (func $bb_atp_b (export "bb_atp_b") (param $h i32) (result i32)
    (i32.const -1)
  )

  ;; ══════════════════════════════════════════════════════════════════════════
  ;; bb_bal — balanced parens (STUB — M-DYN-BAL pending)
  ;; Oracle: bb_bal.c
  ;; ══════════════════════════════════════════════════════════════════════════
  (func $bb_bal_new (export "bb_bal_new") (result i32)
    (call $arena_alloc)
  )
  (func $bb_bal_a (export "bb_bal_a") (param $h i32) (result i32)
    (i32.const -1)   ;; stub — unimplemented
  )
  (func $bb_bal_b (export "bb_bal_b") (param $h i32) (result i32)
    (i32.const -1)
  )

  ;; ── export cursor accessors for exec_stmt ────────────────────────────────
  (func $get_cursor (export "get_cursor") (result i32) (global.get $Δ))
  (func $set_cursor (export "set_cursor") (param $v i32) (global.set $Δ (local.get $v)))
  (func $get_subject_len (export "get_subject_len") (result i32) (global.get $Ω))

)
