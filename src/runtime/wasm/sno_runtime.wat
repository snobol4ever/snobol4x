;; sno_runtime.wat — SNOBOL4 WASM runtime MODULE
;; Standalone module: exports memory + all runtime functions.
;; Programs import from this module rather than inlining it.
;;
;; Memory layout (5 pages = 320KB):
;;   [0  .. 32767]  output buffer  — written by sno_output_*; main() returns fill length
;;   [32768 .. 65535]  string heap — sno_str_alloc() bumps $str_ptr upward
;;   [65536 .. 131071]  program data segment — static string literals (STR_DATA_BASE=65536)
;;   [131072 .. 196607]  Icon gen state — E_TO/E_TO_BY generator slots (ICN_GEN_STATE_BASE=0x20000)
;;   [196608 .. 262143]  Icon activation frame stack (ICON_FRAME_STACK_BASE=0x30004)
;;   [262144 .. 327679]  Array/Table/DATA heap (ARR_HEAP_BASE=0x40000, 64KB)
;;
;; Compile once per session:
;;   wat2wasm --enable-tail-call sno_runtime.wat -o sno_runtime.wasm
;;
;; Milestone: M-SW-1 (fragment); M-SW-A02 (standalone module); M-SW-C02 (array/table/data)
;; Author: Claude Sonnet 4.6

(module
  ;; ── Memory (exported so programs can read output buffer) ──────────────────
  (memory (export "memory") 5)

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

  ;; ── Array / Table / DATA heap ─────────────────────────────────────────────
  ;; ARR_HEAP_BASE = 0x40000 = 262144. Bump allocator grows upward.
  ;;
  ;; Array descriptor header (32 bytes at handle offset):
  ;;   +0  i32 type      1=ARRAY 2=TABLE 3=DATA-instance
  ;;   +4  i32 ndims     number of dimensions (1 or 2 for arrays)
  ;;   +8  i32 lo1       lower bound dim1 (default 1)
  ;;   +12 i32 hi1       upper bound dim1
  ;;   +16 i32 lo2       lower bound dim2 (0 if 1D)
  ;;   +20 i32 hi2       upper bound dim2 (0 if 1D)
  ;;   +24 i32 def_off   default value string offset (0 = null)
  ;;   +28 i32 def_len   default value string length
  ;; Followed by nslots * 8 bytes of slot storage:
  ;;   each slot: i32 val_off, i32 val_len  (string representation)
  ;;   0,0 = null (unset)
  ;;
  ;; Table descriptor header (same 32 bytes, type=2):
  ;;   ndims=0, lo1=capacity (# buckets), hi1=count (# entries used)
  ;; Followed by capacity * 16 bytes of bucket storage:
  ;;   each bucket: i32 key_off, i32 key_len, i32 val_off, i32 val_len
  ;;   key_off=0 means empty bucket
  ;;
  ;; DATA-type descriptor (type=3) stored in DATA type registry:
  ;;   handled separately via $sno_data_define / $sno_data_new etc.
  ;;
  ;; Handle sentinel: programs store array handle in var_X_off,
  ;; var_X_len = 0x7FFF (32767) to distinguish from strings (strings have real lengths).
  ;; MAGIC_HANDLE = 32767

  (global $arr_heap_ptr (mut i32) (i32.const 262144))  ;; ARR_HEAP_BASE

  ;; ── sno_arr_alloc: bump-allocate nbytes from array heap, return offset ────
  (func $sno_arr_alloc (export "sno_arr_alloc")
    (param $nb i32) (result i32)
    (local $p i32)
    (local.set $p (global.get $arr_heap_ptr))
    ;; align to 4 bytes
    (local.set $nb (i32.and (i32.add (local.get $nb) (i32.const 3)) (i32.const -4)))
    (global.set $arr_heap_ptr (i32.add (local.get $p) (local.get $nb)))
    (local.get $p)
  )

  ;; ── sno_array_create: create a 1D array with given size and default value ─
  ;; (size i32, def_off i32, def_len i32) → handle i32
  (func $sno_array_create (export "sno_array_create")
    (param $size i32) (param $def_off i32) (param $def_len i32)
    (result i32)
    (local $h i32) (local $slots_bytes i32) (local $total i32) (local $i i32)
    ;; header = 32 bytes; slots = size * 8 bytes
    (local.set $slots_bytes (i32.mul (local.get $size) (i32.const 8)))
    (local.set $total (i32.add (i32.const 32) (local.get $slots_bytes)))
    (local.set $h (call $sno_arr_alloc (local.get $total)))
    ;; write header
    (i32.store (local.get $h)                          (i32.const 1))         ;; type=ARRAY
    (i32.store (i32.add (local.get $h) (i32.const 4))  (i32.const 1))         ;; ndims=1
    (i32.store (i32.add (local.get $h) (i32.const 8))  (i32.const 1))         ;; lo1=1
    (i32.store (i32.add (local.get $h) (i32.const 12)) (local.get $size))     ;; hi1=size
    (i32.store (i32.add (local.get $h) (i32.const 16)) (i32.const 0))         ;; lo2=0
    (i32.store (i32.add (local.get $h) (i32.const 20)) (i32.const 0))         ;; hi2=0
    (i32.store (i32.add (local.get $h) (i32.const 24)) (local.get $def_off))  ;; def_off
    (i32.store (i32.add (local.get $h) (i32.const 28)) (local.get $def_len))  ;; def_len
    ;; zero all slots (null init)
    (local.set $i (i32.const 0))
    (block $done
      (loop $loop
        (br_if $done (i32.ge_u (local.get $i) (local.get $slots_bytes)))
        (i32.store8
          (i32.add (i32.add (local.get $h) (i32.const 32)) (local.get $i))
          (i32.const 0))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $loop)))
    (local.get $h)
  )

  ;; ── sno_array_create2: create a 2D array ──────────────────────────────────
  ;; (rows i32, cols i32, def_off i32, def_len i32) → handle i32
  (func $sno_array_create2 (export "sno_array_create2")
    (param $rows i32) (param $cols i32) (param $def_off i32) (param $def_len i32)
    (result i32)
    (local $h i32) (local $nslots i32) (local $slots_bytes i32) (local $total i32) (local $i i32)
    (local.set $nslots (i32.mul (local.get $rows) (local.get $cols)))
    (local.set $slots_bytes (i32.mul (local.get $nslots) (i32.const 8)))
    (local.set $total (i32.add (i32.const 32) (local.get $slots_bytes)))
    (local.set $h (call $sno_arr_alloc (local.get $total)))
    (i32.store (local.get $h)                          (i32.const 1))
    (i32.store (i32.add (local.get $h) (i32.const 4))  (i32.const 2))         ;; ndims=2
    (i32.store (i32.add (local.get $h) (i32.const 8))  (i32.const 1))         ;; lo1=1
    (i32.store (i32.add (local.get $h) (i32.const 12)) (local.get $rows))     ;; hi1=rows
    (i32.store (i32.add (local.get $h) (i32.const 16)) (i32.const 1))         ;; lo2=1
    (i32.store (i32.add (local.get $h) (i32.const 20)) (local.get $cols))     ;; hi2=cols
    (i32.store (i32.add (local.get $h) (i32.const 24)) (local.get $def_off))
    (i32.store (i32.add (local.get $h) (i32.const 28)) (local.get $def_len))
    (local.set $i (i32.const 0))
    (block $done
      (loop $loop
        (br_if $done (i32.ge_u (local.get $i) (local.get $slots_bytes)))
        (i32.store8
          (i32.add (i32.add (local.get $h) (i32.const 32)) (local.get $i))
          (i32.const 0))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $loop)))
    (local.get $h)
  )

  ;; ── sno_array_get: get element at 1-based index; returns (off, len) ───────
  ;; (handle i32, idx i32) → (off i32, len i32)
  ;; Returns (0, 0) for null/unset or OOB — caller coerces 0,0 to empty string.
  (func $sno_array_get (export "sno_array_get")
    (param $h i32) (param $idx i32) (result i32 i32)
    (local $lo i32) (local $hi i32) (local $slot_addr i32)
    (local $def_off i32) (local $def_len i32) (local $vo i32) (local $vl i32)
    (local.set $lo  (i32.load (i32.add (local.get $h) (i32.const 8))))
    (local.set $hi  (i32.load (i32.add (local.get $h) (i32.const 12))))
    ;; bounds check
    (if (i32.or
          (i32.lt_s (local.get $idx) (local.get $lo))
          (i32.gt_s (local.get $idx) (local.get $hi)))
      (then (return (i32.const 0) (i32.const 0))))  ;; OOB → fail (null)
    ;; slot address = handle + 32 + (idx - lo) * 8
    (local.set $slot_addr
      (i32.add
        (i32.add (local.get $h) (i32.const 32))
        (i32.mul (i32.sub (local.get $idx) (local.get $lo)) (i32.const 8))))
    (local.set $def_off (i32.load (i32.add (local.get $h) (i32.const 24))))
    (local.set $def_len (i32.load (i32.add (local.get $h) (i32.const 28))))
    (local.set $vo (i32.load               (local.get $slot_addr)))
    (local.set $vl (i32.load (i32.add (local.get $slot_addr) (i32.const 4))))
    ;; if slot is null (0,0) and default set, return default
    (if (i32.and (i32.eqz (local.get $vo)) (i32.eqz (local.get $vl)))
      (then (return (local.get $def_off) (local.get $def_len))))
    (local.get $vo) (local.get $vl)
  )

  ;; ── sno_array_get2: get element at 2D index ───────────────────────────────
  ;; (handle i32, row i32, col i32) → (off i32, len i32)
  (func $sno_array_get2 (export "sno_array_get2")
    (param $h i32) (param $row i32) (param $col i32) (result i32 i32)
    (local $lo1 i32) (local $hi1 i32) (local $lo2 i32) (local $hi2 i32)
    (local $nrows i32) (local $ncols i32) (local $flat i32) (local $slot_addr i32)
    (local $def_off i32) (local $def_len i32) (local $vo i32) (local $vl i32)
    (local.set $lo1 (i32.load (i32.add (local.get $h) (i32.const 8))))
    (local.set $hi1 (i32.load (i32.add (local.get $h) (i32.const 12))))
    (local.set $lo2 (i32.load (i32.add (local.get $h) (i32.const 16))))
    (local.set $hi2 (i32.load (i32.add (local.get $h) (i32.const 20))))
    (if (i32.or
          (i32.or (i32.lt_s (local.get $row) (local.get $lo1))
                  (i32.gt_s (local.get $row) (local.get $hi1)))
          (i32.or (i32.lt_s (local.get $col) (local.get $lo2))
                  (i32.gt_s (local.get $col) (local.get $hi2))))
      (then (return (i32.const 0) (i32.const 0))))
    (local.set $ncols (i32.add (i32.sub (local.get $hi2) (local.get $lo2)) (i32.const 1)))
    (local.set $flat
      (i32.add
        (i32.mul (i32.sub (local.get $row) (local.get $lo1)) (local.get $ncols))
        (i32.sub (local.get $col) (local.get $lo2))))
    (local.set $slot_addr
      (i32.add (i32.add (local.get $h) (i32.const 32))
               (i32.mul (local.get $flat) (i32.const 8))))
    (local.set $def_off (i32.load (i32.add (local.get $h) (i32.const 24))))
    (local.set $def_len (i32.load (i32.add (local.get $h) (i32.const 28))))
    (local.set $vo (i32.load               (local.get $slot_addr)))
    (local.set $vl (i32.load (i32.add (local.get $slot_addr) (i32.const 4))))
    (if (i32.and (i32.eqz (local.get $vo)) (i32.eqz (local.get $vl)))
      (then (return (local.get $def_off) (local.get $def_len))))
    (local.get $vo) (local.get $vl)
  )

  ;; ── sno_array_set: set element at 1-based index ───────────────────────────
  ;; (handle i32, idx i32, val_off i32, val_len i32) → i32 (1=ok, 0=OOB/fail)
  (func $sno_array_set (export "sno_array_set")
    (param $h i32) (param $idx i32) (param $vo i32) (param $vl i32) (result i32)
    (local $lo i32) (local $hi i32) (local $slot_addr i32)
    (local.set $lo (i32.load (i32.add (local.get $h) (i32.const 8))))
    (local.set $hi (i32.load (i32.add (local.get $h) (i32.const 12))))
    (if (i32.or
          (i32.lt_s (local.get $idx) (local.get $lo))
          (i32.gt_s (local.get $idx) (local.get $hi)))
      (then (return (i32.const 0))))
    (local.set $slot_addr
      (i32.add
        (i32.add (local.get $h) (i32.const 32))
        (i32.mul (i32.sub (local.get $idx) (local.get $lo)) (i32.const 8))))
    (i32.store               (local.get $slot_addr)              (local.get $vo))
    (i32.store (i32.add (local.get $slot_addr) (i32.const 4))    (local.get $vl))
    (i32.const 1)
  )

  ;; ── sno_array_set2: set element at 2D index ───────────────────────────────
  (func $sno_array_set2 (export "sno_array_set2")
    (param $h i32) (param $row i32) (param $col i32) (param $vo i32) (param $vl i32)
    (result i32)
    (local $lo1 i32) (local $hi1 i32) (local $lo2 i32) (local $hi2 i32)
    (local $ncols i32) (local $flat i32) (local $slot_addr i32)
    (local.set $lo1 (i32.load (i32.add (local.get $h) (i32.const 8))))
    (local.set $hi1 (i32.load (i32.add (local.get $h) (i32.const 12))))
    (local.set $lo2 (i32.load (i32.add (local.get $h) (i32.const 16))))
    (local.set $hi2 (i32.load (i32.add (local.get $h) (i32.const 20))))
    (if (i32.or
          (i32.or (i32.lt_s (local.get $row) (local.get $lo1))
                  (i32.gt_s (local.get $row) (local.get $hi1)))
          (i32.or (i32.lt_s (local.get $col) (local.get $lo2))
                  (i32.gt_s (local.get $col) (local.get $hi2))))
      (then (return (i32.const 0))))
    (local.set $ncols (i32.add (i32.sub (local.get $hi2) (local.get $lo2)) (i32.const 1)))
    (local.set $flat
      (i32.add
        (i32.mul (i32.sub (local.get $row) (local.get $lo1)) (local.get $ncols))
        (i32.sub (local.get $col) (local.get $lo2))))
    (local.set $slot_addr
      (i32.add (i32.add (local.get $h) (i32.const 32))
               (i32.mul (local.get $flat) (i32.const 8))))
    (i32.store               (local.get $slot_addr)            (local.get $vo))
    (i32.store (i32.add (local.get $slot_addr) (i32.const 4))  (local.get $vl))
    (i32.const 1)
  )

  ;; ── sno_array_prototype: return dimension string e.g. "3" or "2,2" ────────
  ;; (handle i32, out_off i32) → len i32  (writes into out_off in memory)
  ;; We write into the output buffer scratch area at out_off, return byte count.
  (func $sno_array_prototype (export "sno_array_prototype")
    (param $h i32) (param $out i32) (result i32)
    (local $ndims i32) (local $hi1 i32) (local $lo1 i32)
    (local $hi2 i32) (local $lo2 i32) (local $n1 i32) (local $n2 i32)
    (local $pos i32) (local $val i32) (local $tmp i32)
    (local $buf i32) (local $blen i32) (local $digit i32) (local $b i32)
    (local.set $ndims (i32.load (i32.add (local.get $h) (i32.const 4))))
    (local.set $lo1  (i32.load (i32.add (local.get $h) (i32.const 8))))
    (local.set $hi1  (i32.load (i32.add (local.get $h) (i32.const 12))))
    (local.set $lo2  (i32.load (i32.add (local.get $h) (i32.const 16))))
    (local.set $hi2  (i32.load (i32.add (local.get $h) (i32.const 20))))
    (local.set $n1 (i32.add (i32.sub (local.get $hi1) (local.get $lo1)) (i32.const 1)))
    (local.set $n2 (i32.add (i32.sub (local.get $hi2) (local.get $lo2)) (i32.const 1)))
    (local.set $pos (i32.const 0))
    ;; write n1 as decimal
    (local.set $val (local.get $n1))
    ;; write digits backwards into scratch at $out+12
    (local.set $b (i32.const 0))
    (block $itoa_done
      (loop $itoa_loop
        (local.set $digit (i32.rem_u (local.get $val) (i32.const 10)))
        (i32.store8
          (i32.add (i32.add (local.get $out) (i32.const 23)) (i32.sub (i32.const 0) (local.get $b)))
          (i32.add (local.get $digit) (i32.const 48)))
        (local.set $val (i32.div_u (local.get $val) (i32.const 10)))
        (local.set $b (i32.add (local.get $b) (i32.const 1)))
        (br_if $itoa_done (i32.eqz (local.get $val)))
        (br $itoa_loop)))
    ;; copy digits forward to out+0
    (local.set $tmp (i32.const 0))
    (block $cp_done
      (loop $cp_loop
        (br_if $cp_done (i32.ge_u (local.get $tmp) (local.get $b)))
        (i32.store8
          (i32.add (local.get $out) (local.get $tmp))
          (i32.load8_u
            (i32.add
              (i32.add (local.get $out) (i32.const 23))
              (i32.sub (i32.sub (local.get $b) (i32.const 1)) (local.get $tmp)))))
        (local.set $tmp (i32.add (local.get $tmp) (i32.const 1)))
        (br $cp_loop)))
    (local.set $pos (local.get $b))
    ;; if 2D, append "," then n2
    (if (i32.ge_s (local.get $ndims) (i32.const 2))
      (then
        (i32.store8 (i32.add (local.get $out) (local.get $pos)) (i32.const 44)) ;; ','
        (local.set $pos (i32.add (local.get $pos) (i32.const 1)))
        ;; itoa n2
        (local.set $val (local.get $n2))
        (local.set $b (i32.const 0))
        (block $itoa2_done
          (loop $itoa2_loop
            (local.set $digit (i32.rem_u (local.get $val) (i32.const 10)))
            (i32.store8
              (i32.add (i32.add (local.get $out) (i32.const 23)) (i32.sub (i32.const 0) (local.get $b)))
              (i32.add (local.get $digit) (i32.const 48)))
            (local.set $val (i32.div_u (local.get $val) (i32.const 10)))
            (local.set $b (i32.add (local.get $b) (i32.const 1)))
            (br_if $itoa2_done (i32.eqz (local.get $val)))
            (br $itoa2_loop)))
        (local.set $tmp (i32.const 0))
        (block $cp2_done
          (loop $cp2_loop
            (br_if $cp2_done (i32.ge_u (local.get $tmp) (local.get $b)))
            (i32.store8
              (i32.add (local.get $out) (i32.add (local.get $pos) (local.get $tmp)))
              (i32.load8_u
                (i32.add
                  (i32.add (local.get $out) (i32.const 23))
                  (i32.sub (i32.sub (local.get $b) (i32.const 1)) (local.get $tmp)))))
            (local.set $tmp (i32.add (local.get $tmp) (i32.const 1)))
            (br $cp2_loop)))
        (local.set $pos (i32.add (local.get $pos) (local.get $b)))))
    (local.get $pos)
  )

  ;; ── TABLE implementation ───────────────────────────────────────────────────
  ;; Table header: type=2, ndims=0, lo1=capacity, hi1=count
  ;; Buckets follow header (32 bytes): capacity * 16 bytes
  ;;   bucket: key_off(4) key_len(4) val_off(4) val_len(4)
  ;;   empty bucket: key_off=0, key_len=0

  (func $sno_table_create (export "sno_table_create")
    (param $cap i32) (result i32)
    (local $h i32) (local $total i32) (local $i i32) (local $bsize i32)
    ;; minimum 8 buckets, round cap up to power of 2 for simple modulo
    (if (i32.lt_s (local.get $cap) (i32.const 8))
      (then (local.set $cap (i32.const 8))))
    (local.set $total (i32.add (i32.const 32)
                               (i32.mul (local.get $cap) (i32.const 16))))
    (local.set $h (call $sno_arr_alloc (local.get $total)))
    (i32.store (local.get $h)                          (i32.const 2))        ;; type=TABLE
    (i32.store (i32.add (local.get $h) (i32.const 4))  (i32.const 0))        ;; ndims=0
    (i32.store (i32.add (local.get $h) (i32.const 8))  (local.get $cap))     ;; lo1=capacity
    (i32.store (i32.add (local.get $h) (i32.const 12)) (i32.const 0))        ;; hi1=count
    (i32.store (i32.add (local.get $h) (i32.const 16)) (i32.const 0))
    (i32.store (i32.add (local.get $h) (i32.const 20)) (i32.const 0))
    (i32.store (i32.add (local.get $h) (i32.const 24)) (i32.const 0))
    (i32.store (i32.add (local.get $h) (i32.const 28)) (i32.const 0))
    ;; zero all buckets
    (local.set $i (i32.const 0))
    (local.set $bsize (i32.mul (local.get $cap) (i32.const 16)))
    (block $done
      (loop $loop
        (br_if $done (i32.ge_u (local.get $i) (local.get $bsize)))
        (i32.store8 (i32.add (i32.add (local.get $h) (i32.const 32)) (local.get $i)) (i32.const 0))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $loop)))
    (local.get $h)
  )

  ;; hash: simple FNV-1a over key string bytes
  (func $sno_hash (param $ko i32) (param $kl i32) (result i32)
    (local $h i32) (local $i i32)
    (local.set $h (i32.const 0x811c9dc5))
    (local.set $i (i32.const 0))
    (block $done
      (loop $loop
        (br_if $done (i32.ge_u (local.get $i) (local.get $kl)))
        (local.set $h
          (i32.mul
            (i32.xor (local.get $h)
                     (i32.load8_u (i32.add (local.get $ko) (local.get $i))))
            (i32.const 0x01000193)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $loop)))
    (local.get $h)
  )

  ;; sno_str_match_exact: 1 if mem[a..a+len] == mem[b..b+len], 0 otherwise
  (func $sno_str_match_exact (param $a i32) (param $b i32) (param $len i32) (result i32)
    (local $i i32)
    (local.set $i (i32.const 0))
    (block $mismatch
      (loop $loop
        (br_if $mismatch (i32.ge_u (local.get $i) (local.get $len)))
        (if (i32.ne
              (i32.load8_u (i32.add (local.get $a) (local.get $i)))
              (i32.load8_u (i32.add (local.get $b) (local.get $i))))
          (then (return (i32.const 0))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $loop)))
    (i32.const 1)
  )

  ;; sno_table_get: (handle i32, key_off i32, key_len i32) → (val_off i32, val_len i32)
  (func $sno_table_get (export "sno_table_get")
    (param $h i32) (param $ko i32) (param $kl i32) (result i32 i32)
    (local $cap i32) (local $slot i32) (local $base i32)
    (local $bko i32) (local $bkl i32) (local $probe i32)
    (local.set $cap (i32.load (i32.add (local.get $h) (i32.const 8))))
    (local.set $slot
      (i32.rem_u (call $sno_hash (local.get $ko) (local.get $kl)) (local.get $cap)))
    (local.set $probe (i32.const 0))
    (block $found
      (loop $search
        (br_if $found (i32.ge_u (local.get $probe) (local.get $cap)))
        (local.set $base
          (i32.add (i32.add (local.get $h) (i32.const 32))
                   (i32.mul
                     (i32.rem_u (i32.add (local.get $slot) (local.get $probe)) (local.get $cap))
                     (i32.const 16))))
        (local.set $bko (i32.load               (local.get $base)))
        (local.set $bkl (i32.load (i32.add (local.get $base) (i32.const 4))))
        ;; empty bucket → key not found → return null
        (if (i32.and (i32.eqz (local.get $bko)) (i32.eqz (local.get $bkl)))
          (then (return (i32.const 0) (i32.const 0))))
        ;; key match?
        (if (i32.and
              (i32.eq (local.get $bkl) (local.get $kl))
              (call $sno_str_match_exact (local.get $bko) (local.get $ko) (local.get $kl)))
          (then
            (return
              (i32.load (i32.add (local.get $base) (i32.const 8)))
              (i32.load (i32.add (local.get $base) (i32.const 12))))))
        (local.set $probe (i32.add (local.get $probe) (i32.const 1)))
        (br $search)))
    (i32.const 0) (i32.const 0)
  )

  ;; sno_table_set: (handle i32, key_off i32, key_len i32, val_off i32, val_len i32) → void
  (func $sno_table_set (export "sno_table_set")
    (param $h i32) (param $ko i32) (param $kl i32) (param $vo i32) (param $vl i32)
    (local $cap i32) (local $slot i32) (local $base i32)
    (local $bko i32) (local $bkl i32) (local $probe i32)
    (local.set $cap (i32.load (i32.add (local.get $h) (i32.const 8))))
    (local.set $slot
      (i32.rem_u (call $sno_hash (local.get $ko) (local.get $kl)) (local.get $cap)))
    (local.set $probe (i32.const 0))
    (block $placed
      (loop $search
        (br_if $placed (i32.ge_u (local.get $probe) (local.get $cap)))
        (local.set $base
          (i32.add (i32.add (local.get $h) (i32.const 32))
                   (i32.mul
                     (i32.rem_u (i32.add (local.get $slot) (local.get $probe)) (local.get $cap))
                     (i32.const 16))))
        (local.set $bko (i32.load               (local.get $base)))
        (local.set $bkl (i32.load (i32.add (local.get $base) (i32.const 4))))
        ;; empty or matching bucket → place here
        (if (i32.or
              (i32.and (i32.eqz (local.get $bko)) (i32.eqz (local.get $bkl)))
              (i32.and
                (i32.eq (local.get $bkl) (local.get $kl))
                (call $sno_str_match_exact (local.get $bko) (local.get $ko) (local.get $kl))))
          (then
            (i32.store               (local.get $base)              (local.get $ko))
            (i32.store (i32.add (local.get $base) (i32.const 4))    (local.get $kl))
            (i32.store (i32.add (local.get $base) (i32.const 8))    (local.get $vo))
            (i32.store (i32.add (local.get $base) (i32.const 12))   (local.get $vl))
            ;; increment count if new key (was empty)
            (if (i32.and (i32.eqz (local.get $bko)) (i32.eqz (local.get $bkl)))
              (then
                (i32.store (i32.add (local.get $h) (i32.const 12))
                  (i32.add (i32.load (i32.add (local.get $h) (i32.const 12))) (i32.const 1)))))
            (br $placed)))
        (local.set $probe (i32.add (local.get $probe) (i32.const 1)))
        (br $search)))
  )

  ;; sno_table_count: number of entries in table
  (func $sno_table_count (export "sno_table_count")
    (param $h i32) (result i32)
    (i32.load (i32.add (local.get $h) (i32.const 12)))
  )

  ;; sno_table_get_bucket: get key/val at bucket index (for table→array conversion)
  ;; (handle i32, bi i32) → (key_off, key_len, val_off, val_len) i32×4
  (func $sno_table_get_bucket (export "sno_table_get_bucket")
    (param $h i32) (param $bi i32) (result i32 i32 i32 i32)
    (local $base i32)
    (local.set $base
      (i32.add (i32.add (local.get $h) (i32.const 32))
               (i32.mul (local.get $bi) (i32.const 16))))
    (i32.load               (local.get $base))
    (i32.load (i32.add (local.get $base) (i32.const 4)))
    (i32.load (i32.add (local.get $base) (i32.const 8)))
    (i32.load (i32.add (local.get $base) (i32.const 12)))
  )

  ;; sno_table_cap: capacity of table (for iteration)
  (func $sno_table_cap (export "sno_table_cap")
    (param $h i32) (result i32)
    (i32.load (i32.add (local.get $h) (i32.const 8)))
  )

  ;; ── DATA type registry ────────────────────────────────────────────────────
  ;; DATA registry: linear array of type descriptors stored in arr heap.
  ;; Registry header at DATA_REG_BASE = ARR_HEAP_BASE + 0x8000 = 0x48000 = 294912
  ;; Each type entry:
  ;;   +0  i32 name_off    type name string offset
  ;;   +4  i32 name_len
  ;;   +8  i32 nfields     number of fields
  ;;   +12 i32 fields_ptr  pointer to field name array (8 bytes each: off,len)
  ;; Max 64 types.
  ;; DATA instance handle: same arr heap bump alloc, type=3
  ;;   header +0=3, +4=type_idx, +8..+28=unused
  ;;   followed by nfields * 8 bytes (val_off, val_len)

  (global $data_reg_ptr (mut i32) (i32.const 294912))  ;; DATA_REG_BASE

  (func $sno_data_define (export "sno_data_define")
    ;; Register a DATA type: name_off/len + array of field name offsets/lens
    ;; (name_off i32, name_len i32, nfields i32, field_names_ptr i32) → type_idx i32
    (param $no i32) (param $nl i32) (param $nf i32) (param $fp i32) (result i32)
    (local $e i32) (local $idx i32)
    (local.set $e (global.get $data_reg_ptr))
    (i32.store               (local.get $e)              (local.get $no))
    (i32.store (i32.add (local.get $e) (i32.const 4))    (local.get $nl))
    (i32.store (i32.add (local.get $e) (i32.const 8))    (local.get $nf))
    (i32.store (i32.add (local.get $e) (i32.const 12))   (local.get $fp))
    (global.set $data_reg_ptr (i32.add (local.get $e) (i32.const 16)))
    ;; return type index = (e - DATA_REG_BASE) / 16
    (i32.div_u (i32.sub (local.get $e) (i32.const 294912)) (i32.const 16))
  )

  (func $sno_data_new (export "sno_data_new")
    ;; Create new DATA instance: (type_idx i32, nfields i32) → handle i32
    (param $ti i32) (param $nf i32) (result i32)
    (local $h i32) (local $total i32) (local $i i32) (local $slots_bytes i32)
    (local.set $slots_bytes (i32.mul (local.get $nf) (i32.const 8)))
    (local.set $total (i32.add (i32.const 32) (local.get $slots_bytes)))
    (local.set $h (call $sno_arr_alloc (local.get $total)))
    (i32.store (local.get $h)                          (i32.const 3))         ;; type=DATA
    (i32.store (i32.add (local.get $h) (i32.const 4))  (local.get $ti))       ;; type_idx
    (i32.store (i32.add (local.get $h) (i32.const 8))  (local.get $nf))       ;; nfields
    ;; zero all field slots
    (local.set $i (i32.const 0))
    (block $done
      (loop $loop
        (br_if $done (i32.ge_u (local.get $i) (local.get $slots_bytes)))
        (i32.store8 (i32.add (i32.add (local.get $h) (i32.const 32)) (local.get $i)) (i32.const 0))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $loop)))
    (local.get $h)
  )

  (func $sno_data_get_field (export "sno_data_get_field")
    ;; (handle i32, field_idx i32) → (off i32, len i32)
    (param $h i32) (param $fi i32) (result i32 i32)
    (local $slot_addr i32)
    (local.set $slot_addr
      (i32.add (i32.add (local.get $h) (i32.const 32))
               (i32.mul (local.get $fi) (i32.const 8))))
    (i32.load               (local.get $slot_addr))
    (i32.load (i32.add (local.get $slot_addr) (i32.const 4)))
  )

  (func $sno_data_set_field (export "sno_data_set_field")
    ;; (handle i32, field_idx i32, val_off i32, val_len i32) → void
    (param $h i32) (param $fi i32) (param $vo i32) (param $vl i32)
    (local $slot_addr i32)
    (local.set $slot_addr
      (i32.add (i32.add (local.get $h) (i32.const 32))
               (i32.mul (local.get $fi) (i32.const 8))))
    (i32.store               (local.get $slot_addr)            (local.get $vo))
    (i32.store (i32.add (local.get $slot_addr) (i32.const 4))  (local.get $vl))
  )

  ;; sno_handle_type: return type tag from a handle (1=array,2=table,3=data, 0=not a handle)
  (func $sno_handle_type (export "sno_handle_type")
    (param $h i32) (result i32)
    (if (i32.lt_u (local.get $h) (i32.const 262144)) (then (return (i32.const 0))))
    (i32.load (local.get $h))
  )

)
