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
)
