;; pl_runtime.wat — Prolog WebAssembly runtime
;;
;; Memory layout (2 pages = 128KB):
;;   [0..8191]      output buffer
;;   [8192..32767]  string/atom heap (growing upward)
;;   [32768..49151] variable env frames (growing upward)
;;   [49152..57343] trail stack (growing upward, 8KB)
;;   [57344..131071] term heap (growing upward, 64KB)
;;
;; Import namespace: "pl" — used by emit_wasm_prolog.c programs.
;;
;; Milestones:
;;   M-PW-SCAFFOLD  stub (PW-1 2026-03-30)
;;   M-PW-HELLO     pl_output_str/nl/flush confirmed working
;;   M-PW-A01       trail_mark/unwind + pl_unify_atom + pl_var_bind/deref

(module
  (memory (export "memory") 2)   ;; 128KB

  ;; ── Output buffer ──────────────────────────────────────────────────────
  (global $out_pos (mut i32) (i32.const 0))  ;; current write position

  ;; Write a string (offset, len) to output buffer
  (func (export "output_str") (param $off i32) (param $len i32)
    (local $i i32)
    (local.set $i (i32.const 0))
    (block $done
      (loop $copy
        (br_if $done (i32.ge_u (local.get $i) (local.get $len)))
        (i32.store8
          (global.get $out_pos)
          (i32.load8_u (i32.add (local.get $off) (local.get $i))))
        (global.set $out_pos (i32.add (global.get $out_pos) (i32.const 1)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $copy)
      )
    )
  )

  ;; Write newline
  (func (export "output_nl")
    (i32.store8 (global.get $out_pos) (i32.const 10))  ;; '\n'
    (global.set $out_pos (i32.add (global.get $out_pos) (i32.const 1)))
  )

  ;; Return byte count (used by run_wasm.js to slice memory buffer)
  (func (export "output_flush") (result i32)
    (global.get $out_pos)
  )

  ;; ── Trail ───────────────────────────────────────────────────────────────
  ;; Trail stack: array of i32 slot addresses at offset 49152
  ;; trail_top: index into trail array
  (global $trail_top (mut i32) (i32.const 0))
  (global $TRAIL_BASE i32 (i32.const 49152))

  ;; Save current trail top — returns mark (i32)
  (func (export "trail_mark") (result i32)
    (global.get $trail_top)
  )

  ;; Unwind trail back to mark: zero out bound variable slots
  (func (export "trail_unwind") (param $mark i32)
    (local $i i32)
    (local.set $i (global.get $trail_top))
    (block $done
      (loop $unwind
        (br_if $done (i32.le_u (local.get $i) (local.get $mark)))
        (local.set $i (i32.sub (local.get $i) (i32.const 1)))
        ;; zero the slot pointed to by trail[i]
        (i32.store
          (i32.load
            (i32.add
              (global.get $TRAIL_BASE)
              (i32.mul (local.get $i) (i32.const 4))))
          (i32.const 0))
        (br $unwind)
      )
    )
    (global.set $trail_top (local.get $mark))
  )

  ;; ── Variable binding ────────────────────────────────────────────────────
  ;; Stub: bind slot $slot to value $val, push slot to trail
  (func (export "var_bind") (param $slot i32) (param $val i32)
    ;; push slot address to trail
    (i32.store
      (i32.add
        (global.get $TRAIL_BASE)
        (i32.mul (global.get $trail_top) (i32.const 4)))
      (local.get $slot))
    (global.set $trail_top (i32.add (global.get $trail_top) (i32.const 1)))
    ;; write val into slot
    (i32.store (local.get $slot) (local.get $val))
  )

  ;; Dereference a variable slot (follow chain until unbound or non-var)
  (func (export "var_deref") (param $slot i32) (result i32)
    ;; stub: return slot value directly (no ref chains yet)
    (i32.load (local.get $slot))
  )

  ;; ── Unification (atom) ─────────────────────────────────────────────────
  ;; Compare two atom ids; return 1 on success, 0 on failure
  (func (export "unify_atom") (param $a i32) (param $b i32) (result i32)
    (i32.eq (local.get $a) (local.get $b))
  )

  ;; ── Atom heap allocator ────────────────────────────────────────────────
  ;; Atom heap grows upward from 8192 (after atom table entries).
  ;; We use offset 16384 as the start of dynamically allocated atom strings
  ;; (atom table data section occupies 8192..~10000 for ≤256 atoms).
  (global $atom_heap_top (mut i32) (i32.const 16384))

  ;; ── int_to_atom: convert i32 integer → atom_id ────────────────────────
  ;; Writes decimal representation into atom heap, returns atom_id.
  ;; atom_id encodes: upper 16 bits = 0xFFFF (dynamic), lower 16 bits = offset
  ;; Simpler: encode atom as {off, len} stored at a known scratch area (24576).
  ;; We use a single scratch slot per call (not thread-safe, but WASM is single-threaded).
  ;; Scratch atom table entry at 24576: {i32 str_off, i32 str_len}
  ;; String data written at atom_heap_top.
  ;; Returns a "dynamic atom id" = 3000 + sequential index stored at 24576+.
  ;;
  ;; Simpler approach: write int string directly to atom_heap_top,
  ;; store {off,len} at scratch area 24576, return magic id 0xFFFF (special: use off directly).
  ;;
  ;; Actual approach used by emitter: atom_id 0 = unbound, ids 1..N = compile-time atoms.
  ;; For runtime integers, we write string to heap, patch atom table at id=DYNAMIC_BASE+i.
  ;; Since we only need write(X) after is/2, and write(X) uses atom_table[id*8],
  ;; we allocate a new atom_id dynamically by extending atom_table in memory.
  ;;
  ;; Atom table base = 8192. Each entry = 8 bytes. Compile-time atoms fill ids 0..N-1.
  ;; We use a global $dyn_atom_next starting at compile-time atom count (passed via global).
  ;; But we don't know compile-time count at runtime. Solution: use a fixed dynamic slot.
  ;; We write int string at atom_heap_top, store entry at atom_table[DYN_SLOT*8],
  ;; and always use DYN_SLOT=255 (last slot, unlikely to collide with compile atoms < 100).
  ;; Returns 255 always. write(X) will use slot 255's table entry.
  ;; This works because is/2 result is immediately written before next is/2 call.
  (global $DYN_ATOM_SLOT i32 (i32.const 255))
  (func (export "int_to_atom") (param $n i32) (result i32)
    (local $str_start i32)
    (local $pos i32)
    (local $tmp i32)
    (local $neg i32)
    (local $len i32)
    (local $entry i32)
    ;; Write decimal digits of $n to atom_heap_top
    (local.set $str_start (global.get $atom_heap_top))
    (local.set $pos (local.get $str_start))
    ;; Handle negative
    (local.set $neg (i32.const 0))
    (if (i32.lt_s (local.get $n) (i32.const 0))
      (then
        (i32.store8 (local.get $pos) (i32.const 45)) ;; '-'
        (local.set $pos (i32.add (local.get $pos) (i32.const 1)))
        (local.set $n (i32.sub (i32.const 0) (local.get $n)))
        (local.set $neg (i32.const 1))
      )
    )
    ;; Write digits in reverse then reverse the digit portion
    (local.set $tmp (local.get $pos)) ;; digit_start
    (block $done_digits
      (loop $digit_loop
        (i32.store8 (local.get $pos)
          (i32.add (i32.const 48) (i32.rem_u (local.get $n) (i32.const 10))))
        (local.set $pos (i32.add (local.get $pos) (i32.const 1)))
        (local.set $n (i32.div_u (local.get $n) (i32.const 10)))
        (br_if $done_digits (i32.eqz (local.get $n)))
        (br $digit_loop)
      )
    )
    ;; Reverse digits (tmp..pos-1)
    (local.set $len (i32.sub (local.get $pos) (local.get $tmp))) ;; digit count
    (block $rev_done
      (loop $rev
        (br_if $rev_done (i32.le_s (local.get $len) (i32.const 1)))
        ;; swap tmp[0] and tmp[len-1]
        (local.set $neg ;; reuse $neg as swap temp byte
          (i32.load8_u (local.get $tmp)))
        (i32.store8 (local.get $tmp)
          (i32.load8_u (i32.add (local.get $tmp) (i32.sub (local.get $len) (i32.const 1)))))
        (i32.store8
          (i32.add (local.get $tmp) (i32.sub (local.get $len) (i32.const 1)))
          (local.get $neg))
        (local.set $tmp (i32.add (local.get $tmp) (i32.const 1)))
        (local.set $len (i32.sub (local.get $len) (i32.const 2)))
        (br $rev)
      )
    )
    ;; total length = pos - str_start
    (local.set $len (i32.sub (local.get $pos) (local.get $str_start)))
    ;; advance heap
    (global.set $atom_heap_top (local.get $pos))
    ;; write atom table entry at slot 255: {str_start, len}
    (local.set $entry (i32.add (i32.const 8192) (i32.mul (global.get $DYN_ATOM_SLOT) (i32.const 8))))
    (i32.store (local.get $entry) (local.get $str_start))
    (i32.store (i32.add (local.get $entry) (i32.const 4)) (local.get $len))
    ;; return slot id 255
    (global.get $DYN_ATOM_SLOT)
  )

  ;; ── atom_to_int: read decimal string of atom → i32 ────────────────────
  ;; Used by comparisons: load atom_id, get string, parse decimal.
  ;; atom_id < 255: static atom, entry at atom_table[id*8]
  ;; atom_id == 255: dynamic slot
  (func (export "atom_to_int") (param $atom_id i32) (result i32)
    (local $entry i32)
    (local $off i32)
    (local $len i32)
    (local $i i32)
    (local $result i32)
    (local $neg i32)
    (local $ch i32)
    (local.set $entry (i32.add (i32.const 8192) (i32.mul (local.get $atom_id) (i32.const 8))))
    (local.set $off (i32.load (local.get $entry)))
    (local.set $len (i32.load (i32.add (local.get $entry) (i32.const 4))))
    (local.set $i (i32.const 0))
    (local.set $result (i32.const 0))
    (local.set $neg (i32.const 0))
    ;; check for leading '-'
    (if (i32.gt_s (local.get $len) (i32.const 0))
      (then
        (if (i32.eq (i32.load8_u (local.get $off)) (i32.const 45))
          (then
            (local.set $neg (i32.const 1))
            (local.set $i (i32.const 1))
          )
        )
      )
    )
    (block $done_parse
      (loop $parse
        (br_if $done_parse (i32.ge_u (local.get $i) (local.get $len)))
        (local.set $ch (i32.load8_u (i32.add (local.get $off) (local.get $i))))
        (local.set $result
          (i32.add
            (i32.mul (local.get $result) (i32.const 10))
            (i32.sub (local.get $ch) (i32.const 48))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $parse)
      )
    )
    (if (local.get $neg)
      (then (local.set $result (i32.sub (i32.const 0) (local.get $result))))
    )
    (local.get $result)
  )
)
