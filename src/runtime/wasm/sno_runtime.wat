;; sno_runtime.wat — SNOBOL4 WASM runtime MODULE
;; Standalone module: exports memory + all runtime functions.
;; Programs import from this module rather than inlining it.
;;
;; Memory layout (4 pages = 256KB):
;;   [0  .. 32767]  output buffer  — written by sno_output_*; main() returns fill length
;;   [32768 .. 65535]  string heap — sno_str_alloc() bumps $str_ptr upward
;;   [65536 .. 131071]  program data segment — static string literals (STR_DATA_BASE=65536)
;;   [131072 .. 196607]  Icon gen state — E_TO/E_TO_BY generator slots (ICN_GEN_STATE_BASE=0x20000)
;;   [196608 .. 262143]  Icon activation frame stack (ICON_FRAME_STACK_BASE=0x30004)
;;
;; Compile once per session:
;;   wat2wasm --enable-tail-call sno_runtime.wat -o sno_runtime.wasm
;;
;; Milestone: M-SW-1 (fragment); M-SW-A02 (standalone module)
;; Author: Claude Sonnet 4.6

(module
  ;; ── Memory (exported so programs can read output buffer) ──────────────────
  (memory (export "memory") 4)

  (global $out_pos (mut i32) (i32.const 0))
  (global $str_ptr (mut i32) (i32.const 32768))

  ;; ── sno_output_str (offset: i32, len: i32) ────────────────────────────────
  (func $sno_output_str (export "sno_output_str") (param $offset i32) (param $len i32)
    (local $i i32)
    (local $dst i32)
    (local.set $dst (global.get $out_pos))
    (local.set $i (i32.const 0))
    (block $done
      (loop $copy
        (br_if $done (i32.ge_u (local.get $i) (local.get $len)))
        (i32.store8
          (i32.add (local.get $dst) (local.get $i))
          (i32.load8_u (i32.add (local.get $offset) (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $copy)
      )
    )
    (i32.store8 (i32.add (local.get $dst) (local.get $len)) (i32.const 10))
    (global.set $out_pos
      (i32.add (local.get $dst) (i32.add (local.get $len) (i32.const 1))))
  )

  ;; ── sno_output_int (val: i64) ─────────────────────────────────────────────
  (func $sno_output_int (export "sno_output_int") (param $val i64)
    (local $pos i32) (local $start i32) (local $end i32)
    (local $tmp i32) (local $neg i32) (local $v i64) (local $digit i32)
    (local.set $pos (global.get $out_pos))
    (local.set $start (local.get $pos))
    (local.set $neg (i32.const 0))
    (local.set $v (local.get $val))
    (if (i64.lt_s (local.get $v) (i64.const 0))
      (then
        (local.set $neg (i32.const 1))
        (local.set $v (i64.sub (i64.const 0) (local.get $v)))))
    (if (i64.eqz (local.get $v))
      (then
        (i32.store8 (local.get $pos) (i32.const 48))
        (local.set $pos (i32.add (local.get $pos) (i32.const 1))))
      (else
        (block $dbreak
          (loop $digits
            (br_if $dbreak (i64.eqz (local.get $v)))
            (local.set $digit (i32.wrap_i64 (i64.rem_u (local.get $v) (i64.const 10))))
            (i32.store8 (local.get $pos) (i32.add (local.get $digit) (i32.const 48)))
            (local.set $pos (i32.add (local.get $pos) (i32.const 1)))
            (local.set $v (i64.div_u (local.get $v) (i64.const 10)))
            (br $digits)
          )
        )
        (if (local.get $neg)
          (then
            (i32.store8 (local.get $pos) (i32.const 45))
            (local.set $pos (i32.add (local.get $pos) (i32.const 1)))))
        (local.set $end (i32.sub (local.get $pos) (i32.const 1)))
        (local.set $tmp (local.get $start))
        (block $rbreak
          (loop $rev
            (br_if $rbreak (i32.ge_u (local.get $tmp) (local.get $end)))
            (local.set $digit (i32.load8_u (local.get $tmp)))
            (i32.store8 (local.get $tmp) (i32.load8_u (local.get $end)))
            (i32.store8 (local.get $end) (local.get $digit))
            (local.set $tmp (i32.add (local.get $tmp) (i32.const 1)))
            (local.set $end (i32.sub (local.get $end) (i32.const 1)))
            (br $rev)
          )
        )
      )
    )
    (i32.store8 (local.get $pos) (i32.const 10))
    (global.set $out_pos (i32.add (local.get $pos) (i32.const 1)))
  )

  ;; ── sno_output_flush () → i32 ─────────────────────────────────────────────
  (func $sno_output_flush (export "sno_output_flush") (result i32)
    (global.get $out_pos)
  )

  ;; ── sno_str_alloc (len: i32) → i32 ───────────────────────────────────────
  (func $sno_str_alloc (export "sno_str_alloc") (param $len i32) (result i32)
    (local $ptr i32)
    (local.set $ptr (global.get $str_ptr))
    (global.set $str_ptr (i32.add (local.get $ptr) (local.get $len)))
    (local.get $ptr)
  )

  ;; ── sno_str_concat ────────────────────────────────────────────────────────
  (func $sno_str_concat (export "sno_str_concat")
    (param $a_off i32) (param $a_len i32)
    (param $b_off i32) (param $b_len i32)
    (result i32 i32)
    (local $total i32) (local $dst i32) (local $i i32)
    (local.set $total (i32.add (local.get $a_len) (local.get $b_len)))
    (local.set $dst (call $sno_str_alloc (local.get $total)))
    (local.set $i (i32.const 0))
    (block $da (loop $la
      (br_if $da (i32.ge_u (local.get $i) (local.get $a_len)))
      (i32.store8 (i32.add (local.get $dst) (local.get $i))
        (i32.load8_u (i32.add (local.get $a_off) (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 0))
    (block $db (loop $lb
      (br_if $db (i32.ge_u (local.get $i) (local.get $b_len)))
      (i32.store8 (i32.add (local.get $dst) (i32.add (local.get $a_len) (local.get $i)))
        (i32.load8_u (i32.add (local.get $b_off) (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.get $dst)
    (local.get $total)
  )

  ;; ── sno_str_eq ────────────────────────────────────────────────────────────
  (func $sno_str_eq (export "sno_str_eq")
    (param $a_off i32) (param $a_len i32)
    (param $b_off i32) (param $b_len i32)
    (result i32)
    (local $i i32)
    (if (i32.ne (local.get $a_len) (local.get $b_len))
      (then (return (i32.const 0))))
    (local.set $i (i32.const 0))
    (block $done (loop $cmp
      (br_if $done (i32.ge_u (local.get $i) (local.get $a_len)))
      (if (i32.ne
            (i32.load8_u (i32.add (local.get $a_off) (local.get $i)))
            (i32.load8_u (i32.add (local.get $b_off) (local.get $i))))
        (then (return (i32.const 0))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $cmp)))
    (i32.const 1)
  )

  ;; ── sno_size (off i32, len i32) → i64 ───────────────────────────────────────
  ;; Returns length of string as SNOBOL4 integer.
  ;; For an integer arg the caller must first convert to string via sno_int_to_str.
  (func $sno_size (export "sno_size")
    (param $off i32) (param $len i32) (result i64)
    (i64.extend_i32_u (local.get $len))
  )

  ;; ── sno_dupl (off i32, len i32, n i64) → (off i32, len i32) ─────────────────
  ;; Replicates the string n times.  n <= 0 → returns empty string.
  (func $sno_dupl (export "sno_dupl")
    (param $off i32) (param $len i32) (param $n i64)
    (result i32 i32)
    (local $total i32) (local $dst i32) (local $i i64) (local $j i32)
    ;; n <= 0 or len == 0 → empty string
    (if (i64.le_s (local.get $n) (i64.const 0))
      (then
        (i32.const 32768)   ;; any valid offset — length 0 means unused
        (i32.const 0)
        (return)))
    (if (i32.eqz (local.get $len))
      (then
        (i32.const 32768)
        (i32.const 0)
        (return)))
    (local.set $total (i32.mul (local.get $len) (i32.wrap_i64 (local.get $n))))
    (local.set $dst (call $sno_str_alloc (local.get $total)))
    (local.set $i (i64.const 0))
    (block $outer (loop $rep
      (br_if $outer (i64.ge_u (local.get $i) (local.get $n)))
      (local.set $j (i32.const 0))
      (block $inner (loop $cp
        (br_if $inner (i32.ge_u (local.get $j) (local.get $len)))
        (i32.store8
          (i32.add (local.get $dst)
            (i32.add (i32.mul (i32.wrap_i64 (local.get $i)) (local.get $len))
                     (local.get $j)))
          (i32.load8_u (i32.add (local.get $off) (local.get $j))))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $cp)))
      (local.set $i (i64.add (local.get $i) (i64.const 1)))
      (br $rep)))
    (local.get $dst)
    (local.get $total)
  )

  ;; ── sno_replace (s_off i32, s_len i32, from_off i32, from_len i32,
  ;;                to_off i32, to_len i32) → (off i32, len i32) ───────────────
  ;; SNOBOL4 REPLACE: build a 256-byte translation table from from/to,
  ;; then translate each byte of the subject through it.
  ;; Characters in from map to corresponding characters in to.
  ;; If from is longer than to, excess chars in from map to nothing (deleted).
  ;; (CSNOBOL4 truncates to to the shorter length — we match that.)
  (func $sno_replace (export "sno_replace")
    (param $s_off   i32) (param $s_len   i32)
    (param $fr_off  i32) (param $fr_len  i32)
    (param $to_off  i32) (param $to_len  i32)
    (result i32 i32)
    (local $tbl i32) (local $dst i32) (local $i i32) (local $c i32) (local $pairs i32)
    ;; allocate 256-byte translation table on heap
    (local.set $tbl (call $sno_str_alloc (i32.const 256)))
    ;; initialize: identity mapping
    (local.set $i (i32.const 0))
    (block $b0 (loop $l0
      (br_if $b0 (i32.ge_u (local.get $i) (i32.const 256)))
      (i32.store8 (i32.add (local.get $tbl) (local.get $i)) (local.get $i))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l0)))
    ;; apply from→to substitutions (only up to min(from_len, to_len) pairs)
    (local.set $pairs
      (if (result i32) (i32.lt_u (local.get $fr_len) (local.get $to_len))
        (then (local.get $fr_len))
        (else (local.get $to_len))))
    (local.set $i (i32.const 0))
    (block $b1 (loop $l1
      (br_if $b1 (i32.ge_u (local.get $i) (local.get $pairs)))
      (local.set $c (i32.load8_u (i32.add (local.get $fr_off) (local.get $i))))
      (i32.store8
        (i32.add (local.get $tbl) (local.get $c))
        (i32.load8_u (i32.add (local.get $to_off) (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l1)))
    ;; allocate result buffer (same length as subject — translation is 1:1)
    (local.set $dst (call $sno_str_alloc (local.get $s_len)))
    ;; translate each byte through the table
    (local.set $i (i32.const 0))
    (block $b2 (loop $l2
      (br_if $b2 (i32.ge_u (local.get $i) (local.get $s_len)))
      (local.set $c (i32.load8_u (i32.add (local.get $s_off) (local.get $i))))
      (i32.store8
        (i32.add (local.get $dst) (local.get $i))
        (i32.load8_u (i32.add (local.get $tbl) (local.get $c))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l2)))
    (local.get $dst)
    (local.get $s_len)
  )

  ;; ── sno_pow (base f64, exp f64) → f64 ─────────────────────────────────────
  (func $sno_pow (export "sno_pow") (param $base f64) (param $exp f64) (result f64)
    (local $result f64) (local $n i64) (local $neg_exp i32)
    (local.set $result (f64.const 1))
    (local.set $n (i64.trunc_f64_s (local.get $exp)))
    (local.set $neg_exp (i64.lt_s (local.get $n) (i64.const 0)))
    (if (local.get $neg_exp)
      (then (local.set $n (i64.sub (i64.const 0) (local.get $n)))))
    (block $done (loop $pow
      (br_if $done (i64.eqz (local.get $n)))
      (local.set $result (f64.mul (local.get $result) (local.get $base)))
      (local.set $n (i64.sub (local.get $n) (i64.const 1)))
      (br $pow)))
    (if (local.get $neg_exp)
      (then (local.set $result (f64.div (f64.const 1) (local.get $result)))))
    (local.get $result)
  )

  ;; ── sno_str_to_int (off i32, len i32) → i64 ───────────────────────────────
  (func $sno_str_to_int (export "sno_str_to_int")
    (param $off i32) (param $len i32) (result i64)
    (local $i i32) (local $neg i32) (local $result i64) (local $c i32)
    (local.set $result (i64.const 0))
    (local.set $neg (i32.const 0))
    (local.set $i (i32.const 0))
    ;; skip leading spaces
    (block $sp_done (loop $sp
      (br_if $sp_done (i32.ge_u (local.get $i) (local.get $len)))
      (local.set $c (i32.load8_u (i32.add (local.get $off) (local.get $i))))
      (br_if $sp_done (i32.ne (local.get $c) (i32.const 32)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $sp)))
    ;; optional sign
    (if (i32.lt_u (local.get $i) (local.get $len))
      (then
        (local.set $c (i32.load8_u (i32.add (local.get $off) (local.get $i))))
        (if (i32.eq (local.get $c) (i32.const 45))
          (then (local.set $neg (i32.const 1))
                (local.set $i (i32.add (local.get $i) (i32.const 1)))))
        (if (i32.eq (local.get $c) (i32.const 43))
          (then (local.set $i (i32.add (local.get $i) (i32.const 1)))))))
    ;; digits
    (block $dbreak (loop $dlp
      (br_if $dbreak (i32.ge_u (local.get $i) (local.get $len)))
      (local.set $c (i32.load8_u (i32.add (local.get $off) (local.get $i))))
      (br_if $dbreak (i32.lt_u (local.get $c) (i32.const 48)))
      (br_if $dbreak (i32.gt_u (local.get $c) (i32.const 57)))
      (local.set $result (i64.add
        (i64.mul (local.get $result) (i64.const 10))
        (i64.extend_i32_u (i32.sub (local.get $c) (i32.const 48)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $dlp)))
    (if (local.get $neg)
      (then (local.set $result (i64.sub (i64.const 0) (local.get $result)))))
    (local.get $result)
  )

  ;; ── sno_int_to_str (val i64) → (off i32, len i32) ─────────────────────────
  (func $sno_int_to_str (export "sno_int_to_str") (param $val i64) (result i32 i32)
    (local $pos i32) (local $start i32) (local $end i32)
    (local $tmp i32) (local $dig i32) (local $neg i32) (local $v i64)
    (local.set $start (global.get $str_ptr))
    (local.set $pos (local.get $start))
    (local.set $neg (i32.wrap_i64 (i64.and
      (i64.shr_u (local.get $val) (i64.const 63)) (i64.const 1))))
    (local.set $v (local.get $val))
    (if (local.get $neg)
      (then (local.set $v (i64.sub (i64.const 0) (local.get $v)))))
    (if (i64.eqz (local.get $v))
      (then
        (i32.store8 (local.get $pos) (i32.const 48))
        (local.set $pos (i32.add (local.get $pos) (i32.const 1))))
      (else
        (block $dbreak (loop $dlp
          (br_if $dbreak (i64.eqz (local.get $v)))
          (local.set $dig (i32.wrap_i64 (i64.rem_u (local.get $v) (i64.const 10))))
          (i32.store8 (local.get $pos) (i32.add (local.get $dig) (i32.const 48)))
          (local.set $pos (i32.add (local.get $pos) (i32.const 1)))
          (local.set $v (i64.div_u (local.get $v) (i64.const 10)))
          (br $dlp)))
        (if (local.get $neg)
          (then
            (i32.store8 (local.get $pos) (i32.const 45))
            (local.set $pos (i32.add (local.get $pos) (i32.const 1)))))
        (local.set $end (i32.sub (local.get $pos) (i32.const 1)))
        (local.set $tmp (local.get $start))
        (block $rbreak (loop $rlp
          (br_if $rbreak (i32.ge_u (local.get $tmp) (local.get $end)))
          (local.set $dig (i32.load8_u (local.get $tmp)))
          (i32.store8 (local.get $tmp) (i32.load8_u (local.get $end)))
          (i32.store8 (local.get $end) (local.get $dig))
          (local.set $tmp (i32.add (local.get $tmp) (i32.const 1)))
          (local.set $end (i32.sub (local.get $end) (i32.const 1)))
          (br $rlp)))))
    (global.set $str_ptr (local.get $pos))
    (local.get $start)
    (i32.sub (local.get $pos) (local.get $start))
  )

  ;; ── sno_float_to_str (val f64) → (off i32, len i32) ──────────────────────
  ;; SNOBOL4 format: 1.0 → "1.", 1.5 → "1.5", strips trailing fractional zeros.
  (func $sno_float_to_str (export "sno_float_to_str") (param $val f64) (result i32 i32)
    (local $start i32) (local $pos i32) (local $neg i32)
    (local $ipart i64) (local $fpart f64) (local $fscale f64)
    (local $fint i64) (local $fdig i32) (local $fstart i32) (local $fend i32)
    (local $tmp i32) (local $dig i32)
    (local.set $start (global.get $str_ptr))
    (local.set $pos (local.get $start))
    ;; negative?
    (local.set $neg (f64.lt (local.get $val) (f64.const 0)))
    (if (local.get $neg)
      (then
        (local.set $val (f64.neg (local.get $val)))
        (i32.store8 (local.get $pos) (i32.const 45))
        (local.set $pos (i32.add (local.get $pos) (i32.const 1)))))
    ;; integer part
    (local.set $ipart (i64.trunc_f64_s (local.get $val)))
    (if (i64.eqz (local.get $ipart))
      (then
        (i32.store8 (local.get $pos) (i32.const 48))
        (local.set $pos (i32.add (local.get $pos) (i32.const 1))))
      (else
        (local.set $tmp (local.get $pos))
        (block $ib (loop $il
          (br_if $ib (i64.eqz (local.get $ipart)))
          (local.set $dig (i32.wrap_i64 (i64.rem_u (local.get $ipart) (i64.const 10))))
          (i32.store8 (local.get $pos) (i32.add (local.get $dig) (i32.const 48)))
          (local.set $pos (i32.add (local.get $pos) (i32.const 1)))
          (local.set $ipart (i64.div_u (local.get $ipart) (i64.const 10)))
          (br $il)))
        (local.set $fend (i32.sub (local.get $pos) (i32.const 1)))
        (block $rb (loop $rl
          (br_if $rb (i32.ge_u (local.get $tmp) (local.get $fend)))
          (local.set $dig (i32.load8_u (local.get $tmp)))
          (i32.store8 (local.get $tmp) (i32.load8_u (local.get $fend)))
          (i32.store8 (local.get $fend) (local.get $dig))
          (local.set $tmp (i32.add (local.get $tmp) (i32.const 1)))
          (local.set $fend (i32.sub (local.get $fend) (i32.const 1)))
          (br $rl)))))
    ;; decimal point always present
    (i32.store8 (local.get $pos) (i32.const 46))
    (local.set $pos (i32.add (local.get $pos) (i32.const 1)))
    ;; fractional part (up to 6 significant digits, strip trailing zeros)
    (local.set $fpart (f64.sub (local.get $val) (f64.convert_i64_s
      (i64.trunc_f64_s (local.get $val)))))
    (local.set $fscale (f64.const 1000000))
    (local.set $fint (i64.trunc_f64_s (f64.add
      (f64.mul (local.get $fpart) (local.get $fscale)) (f64.const 0.5))))
    (if (i64.eqz (local.get $fint))
      (then) ;; no fractional digits — trailing dot only (SNOBOL4: 1. format)
      (else
        (local.set $fstart (local.get $pos))
        ;; write 6 digits (with leading zeros)
        (local.set $tmp (i32.const 0))
        (block $sb (loop $sl
          (br_if $sb (i32.ge_u (local.get $tmp) (i32.const 6)))
          (local.set $fdig (i32.wrap_i64 (i64.rem_u (local.get $fint) (i64.const 10))))
          (i32.store8 (i32.add (local.get $pos) (local.get $tmp)) (i32.add (local.get $fdig) (i32.const 48)))
          (local.set $fint (i64.div_u (local.get $fint) (i64.const 10)))
          (local.set $tmp (i32.add (local.get $tmp) (i32.const 1)))
          (br $sl)))
        ;; these 6 digits are reversed — fix them
        (local.set $fend (i32.add (local.get $pos) (i32.const 5)))
        (local.set $tmp (local.get $pos))
        (block $fb (loop $fl
          (br_if $fb (i32.ge_u (local.get $tmp) (local.get $fend)))
          (local.set $fdig (i32.load8_u (local.get $tmp)))
          (i32.store8 (local.get $tmp) (i32.load8_u (local.get $fend)))
          (i32.store8 (local.get $fend) (local.get $fdig))
          (local.set $tmp (i32.add (local.get $tmp) (i32.const 1)))
          (local.set $fend (i32.sub (local.get $fend) (i32.const 1)))
          (br $fl)))
        ;; strip trailing zeros
        (local.set $pos (i32.add (local.get $pos) (i32.const 6)))
        (block $tz (loop $tzl
          (br_if $tz (i32.le_u (local.get $pos) (local.get $fstart)))
          (br_if $tz (i32.ne (i32.load8_u (i32.sub (local.get $pos) (i32.const 1))) (i32.const 48)))
          (local.set $pos (i32.sub (local.get $pos) (i32.const 1)))
          (br $tzl)))))
    (global.set $str_ptr (local.get $pos))
    (local.get $start)
    (i32.sub (local.get $pos) (local.get $start))
  )

  ;; ── sno_str_to_float (off i32, len i32) → f64 ────────────────────────────
  (func $sno_str_to_float (export "sno_str_to_float")
    (param $off i32) (param $len i32) (result f64)
    (local $i i32) (local $neg i32) (local $c i32)
    (local $ipart f64) (local $fpart f64) (local $fscale f64) (local $in_frac i32)
    (local.set $i (i32.const 0))
    (local.set $ipart (f64.const 0))
    (local.set $fpart (f64.const 0))
    (local.set $fscale (f64.const 1))
    (local.set $neg (i32.const 0))
    (local.set $in_frac (i32.const 0))
    (block $sp_done (loop $sp
      (br_if $sp_done (i32.ge_u (local.get $i) (local.get $len)))
      (local.set $c (i32.load8_u (i32.add (local.get $off) (local.get $i))))
      (br_if $sp_done (i32.ne (local.get $c) (i32.const 32)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $sp)))
    (if (i32.lt_u (local.get $i) (local.get $len))
      (then
        (local.set $c (i32.load8_u (i32.add (local.get $off) (local.get $i))))
        (if (i32.eq (local.get $c) (i32.const 45))
          (then (local.set $neg (i32.const 1))
                (local.set $i (i32.add (local.get $i) (i32.const 1)))))
        (if (i32.eq (local.get $c) (i32.const 43))
          (then (local.set $i (i32.add (local.get $i) (i32.const 1)))))))
    (block $dbreak (loop $dlp
      (br_if $dbreak (i32.ge_u (local.get $i) (local.get $len)))
      (local.set $c (i32.load8_u (i32.add (local.get $off) (local.get $i))))
      (if (i32.eq (local.get $c) (i32.const 46))
        (then
          (local.set $in_frac (i32.const 1))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $dlp)))
      (br_if $dbreak (i32.lt_u (local.get $c) (i32.const 48)))
      (br_if $dbreak (i32.gt_u (local.get $c) (i32.const 57)))
      (if (local.get $in_frac)
        (then
          (local.set $fscale (f64.mul (local.get $fscale) (f64.const 10)))
          (local.set $fpart (f64.add (local.get $fpart)
            (f64.div (f64.convert_i32_u (i32.sub (local.get $c) (i32.const 48)))
                     (local.get $fscale)))))
        (else
          (local.set $ipart (f64.add (f64.mul (local.get $ipart) (f64.const 10))
            (f64.convert_i32_u (i32.sub (local.get $c) (i32.const 48)))))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $dlp)))
    (local.set $ipart (f64.add (local.get $ipart) (local.get $fpart)))
    (if (local.get $neg) (then (local.set $ipart (f64.neg (local.get $ipart)))))
    (local.get $ipart)
  )

  ;; ── sno_lgt (a_off i32, a_len i32, b_off i32, b_len i32) → i32 ──────────
  ;; Returns 1 if string a is lexicographically greater than b, else 0.
  (func $sno_lgt (export "sno_lgt")
    (param $a_off i32) (param $a_len i32)
    (param $b_off i32) (param $b_len i32)
    (result i32)
    (local $i i32) (local $minlen i32) (local $ca i32) (local $cb i32)
    (local.set $minlen
      (if (result i32) (i32.lt_u (local.get $a_len) (local.get $b_len))
        (then (local.get $a_len)) (else (local.get $b_len))))
    (local.set $i (i32.const 0))
    (block $done (loop $cmp
      (br_if $done (i32.ge_u (local.get $i) (local.get $minlen)))
      (local.set $ca (i32.load8_u (i32.add (local.get $a_off) (local.get $i))))
      (local.set $cb (i32.load8_u (i32.add (local.get $b_off) (local.get $i))))
      (if (i32.gt_u (local.get $ca) (local.get $cb)) (then (return (i32.const 1))))
      (if (i32.lt_u (local.get $ca) (local.get $cb)) (then (return (i32.const 0))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $cmp)))
    (i32.gt_u (local.get $a_len) (local.get $b_len))
  )


  ;; ── sno_pat_search (hay_off i32, hay_len i32, ndl_off i32, ndl_len i32, start_cursor i32) → i32
  ;; Cursor-based substring search. Searches haystack[start_cursor..] for needle.
  ;; Returns the cursor position AFTER the match (start_cursor + match_pos + ndl_len),
  ;; or -1 on failure. Empty needle matches at start_cursor, returning start_cursor.
  ;; Used by emit_wasm.c pattern emitter for SEQ chaining.
  (func $sno_pat_search (export "sno_pat_search")
    (param $ho i32) (param $hl i32) (param $no i32) (param $nl i32) (param $cur i32)
    (result i32)
    (local $i i32) (local $j i32) (local $limit i32) (local $match i32)
    (local $search_len i32)
    ;; empty needle → match at cursor
    (if (i32.eqz (local.get $nl)) (then (return (local.get $cur))))
    ;; cursor past end → no match
    (if (i32.ge_u (local.get $cur) (local.get $hl)) (then (return (i32.const -1))))
    ;; search_len = haystack available from cursor
    (local.set $search_len (i32.sub (local.get $hl) (local.get $cur)))
    ;; needle longer than available → no match
    (if (i32.gt_u (local.get $nl) (local.get $search_len)) (then (return (i32.const -1))))
    ;; limit = number of start positions to try
    (local.set $limit (i32.sub (local.get $search_len) (local.get $nl)))
    (local.set $i (i32.const 0))
    (block $outer_done
      (loop $outer
        (br_if $outer_done (i32.gt_u (local.get $i) (local.get $limit)))
        (local.set $j (i32.const 0))
        (local.set $match (i32.const 1))
        (block $inner_done
          (loop $inner
            (br_if $inner_done (i32.ge_u (local.get $j) (local.get $nl)))
            (if (i32.ne
                  (i32.load8_u (i32.add (local.get $ho)
                                        (i32.add (local.get $cur)
                                                 (i32.add (local.get $i) (local.get $j)))))
                  (i32.load8_u (i32.add (local.get $no) (local.get $j))))
              (then
                (local.set $match (i32.const 0))
                (br $inner_done)))
            (local.set $j (i32.add (local.get $j) (i32.const 1)))
            (br $inner)))
        ;; match found: return cursor + i + needle_len
        (if (local.get $match)
          (then
            (return (i32.add (local.get $cur)
                             (i32.add (local.get $i) (local.get $nl))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $outer)))
    (i32.const -1)
  )

  ;; ── sno_str_contains (hay_off i32, hay_len i32, ndl_off i32, ndl_len i32) → i32
  ;; Unanchored substring search. Returns 1 if needle found anywhere in haystack, 0 if not.
  ;; Empty needle always matches (returns 1).
  (func $sno_str_contains (export "sno_str_contains")
    (param $ho i32) (param $hl i32) (param $no i32) (param $nl i32)
    (result i32)
    (local $i i32) (local $j i32) (local $limit i32) (local $match i32)
    ;; empty needle → always match
    (if (i32.eqz (local.get $nl)) (then (return (i32.const 1))))
    ;; needle longer than haystack → no match
    (if (i32.gt_u (local.get $nl) (local.get $hl)) (then (return (i32.const 0))))
    ;; limit = haystack_len - needle_len  (last valid start position)
    (local.set $limit (i32.sub (local.get $hl) (local.get $nl)))
    (local.set $i (i32.const 0))
    (block $outer_done
      (loop $outer
        (br_if $outer_done (i32.gt_u (local.get $i) (local.get $limit)))
        ;; try to match needle at position i
        (local.set $j (i32.const 0))
        (local.set $match (i32.const 1))
        (block $inner_done
          (loop $inner
            (br_if $inner_done (i32.ge_u (local.get $j) (local.get $nl)))
            (if (i32.ne
                  (i32.load8_u (i32.add (local.get $ho) (i32.add (local.get $i) (local.get $j))))
                  (i32.load8_u (i32.add (local.get $no) (local.get $j))))
              (then
                (local.set $match (i32.const 0))
                (br $inner_done)))
            (local.set $j (i32.add (local.get $j) (i32.const 1)))
            (br $inner)))
        (if (local.get $match) (then (return (i32.const 1))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $outer)))
    (i32.const 0)
  )

  ;; ── Character-class pattern helpers (M-SW-B05) ───────────────────────────
  ;; All take (subj_off subj_len set_off set_len cursor) → new_cursor or -1.
  ;; "set" is a string of characters that form the class (e.g. "aeiou").
  ;; A character is "in set" if it equals any byte in set[0..set_len-1].

  ;; sno_char_in_set — internal helper: is byte $ch in set[so..so+sl-1]?
  ;; Returns i32: 1=yes 0=no.  NOT exported.
  (func $sno_char_in_set (param $ch i32) (param $so i32) (param $sl i32) (result i32)
    (local $k i32)
    (local.set $k (i32.const 0))
    (block $done
      (loop $scan
        (br_if $done (i32.ge_u (local.get $k) (local.get $sl)))
        (if (i32.eq
              (local.get $ch)
              (i32.load8_u (i32.add (local.get $so) (local.get $k))))
          (then (return (i32.const 1))))
        (local.set $k (i32.add (local.get $k) (i32.const 1)))
        (br $scan)))
    (i32.const 0)
  )

  ;; sno_any — match exactly one char in set at cursor; return cursor+1 or -1.
  (func $sno_any (export "sno_any")
    (param $ho i32) (param $hl i32) (param $so i32) (param $sl i32) (param $cur i32)
    (result i32)
    ;; cursor past end → fail
    (if (i32.ge_u (local.get $cur) (local.get $hl)) (then (return (i32.const -1))))
    ;; empty set → fail
    (if (i32.eqz (local.get $sl)) (then (return (i32.const -1))))
    (if (call $sno_char_in_set
              (i32.load8_u (i32.add (local.get $ho) (local.get $cur)))
              (local.get $so) (local.get $sl))
      (then (return (i32.add (local.get $cur) (i32.const 1)))))
    (i32.const -1)
  )

  ;; sno_notany — match exactly one char NOT in set; return cursor+1 or -1.
  (func $sno_notany (export "sno_notany")
    (param $ho i32) (param $hl i32) (param $so i32) (param $sl i32) (param $cur i32)
    (result i32)
    (if (i32.ge_u (local.get $cur) (local.get $hl)) (then (return (i32.const -1))))
    (if (i32.eqz
          (call $sno_char_in_set
                (i32.load8_u (i32.add (local.get $ho) (local.get $cur)))
                (local.get $so) (local.get $sl)))
      (then (return (i32.add (local.get $cur) (i32.const 1)))))
    (i32.const -1)
  )

  ;; sno_span — match one-or-more consecutive chars in set; return new cursor or -1.
  (func $sno_span (export "sno_span")
    (param $ho i32) (param $hl i32) (param $so i32) (param $sl i32) (param $cur i32)
    (result i32)
    (local $c i32)
    ;; must match at least one
    (if (i32.ge_u (local.get $cur) (local.get $hl)) (then (return (i32.const -1))))
    (if (i32.eqz (local.get $sl)) (then (return (i32.const -1))))
    (if (i32.eqz
          (call $sno_char_in_set
                (i32.load8_u (i32.add (local.get $ho) (local.get $cur)))
                (local.get $so) (local.get $sl)))
      (then (return (i32.const -1))))
    (local.set $c (i32.add (local.get $cur) (i32.const 1)))
    (block $done
      (loop $loop
        (br_if $done (i32.ge_u (local.get $c) (local.get $hl)))
        (br_if $done
          (i32.eqz
            (call $sno_char_in_set
                  (i32.load8_u (i32.add (local.get $ho) (local.get $c)))
                  (local.get $so) (local.get $sl))))
        (local.set $c (i32.add (local.get $c) (i32.const 1)))
        (br $loop)))
    (local.get $c)
  )

  ;; sno_break — advance cursor up to (but not including) first char in set.
  ;; Succeeds even if zero chars consumed (cursor at set-char already → match 0).
  ;; Returns new cursor (may equal $cur) or -1 if end of string reached without
  ;; finding a set member.
  (func $sno_break (export "sno_break")
    (param $ho i32) (param $hl i32) (param $so i32) (param $sl i32) (param $cur i32)
    (result i32)
    (local $c i32)
    (local.set $c (local.get $cur))
    (block $done
      (loop $loop
        (br_if $done (i32.ge_u (local.get $c) (local.get $hl)))
        (br_if $done
          (call $sno_char_in_set
                (i32.load8_u (i32.add (local.get $ho) (local.get $c)))
                (local.get $so) (local.get $sl)))
        (local.set $c (i32.add (local.get $c) (i32.const 1)))
        (br $loop)))
    ;; if we reached end without finding delimiter → fail
    (if (i32.ge_u (local.get $c) (local.get $hl)) (then (return (i32.const -1))))
    (local.get $c)
  )

  ;; sno_breakx — like BREAK but also consumes the delimiter char.
  ;; Returns cursor past the delimiter, or -1.
  (func $sno_breakx (export "sno_breakx")
    (param $ho i32) (param $hl i32) (param $so i32) (param $sl i32) (param $cur i32)
    (result i32)
    (local $c i32)
    (local.set $c
      (call $sno_break
            (local.get $ho) (local.get $hl)
            (local.get $so) (local.get $sl) (local.get $cur)))
    (if (i32.lt_s (local.get $c) (i32.const 0)) (then (return (i32.const -1))))
    ;; advance past the delimiter
    (i32.add (local.get $c) (i32.const 1))
  )

)
