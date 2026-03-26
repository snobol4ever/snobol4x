```SNOBOL4
*  SCRIP DEMO8 -- Sort 8 integers (SNOBOL4 section)
*  Idiom: Gimpel BSORT insertion sort on ARRAY; LGT drives shifts
        &TRIM = 1
        DEFINE('BSORT(A,I,N)J,K,V')         :(BSORT_END)
BSORT   J  = I
BSORT_1 J  = J + 1  LT(J,N)                :F(RETURN)
        K  = J
        V  = A<J>
BSORT_2 K  = K - 1  GT(K,I)                :F(BSORT_RO)
        A<K + 1>  = LGT(A<K>,V)  A<K>      :S(BSORT_2)
        A<K + 1>  = V                       :(BSORT_1)
BSORT_RO A<I> = V                          :(BSORT_1)
BSORT_END
        A      = ARRAY(8)
        A<1>   = 5  ;  A<2>  = 3  ;  A<3>  = 8  ;  A<4>  = 1
        A<5>   = 9  ;  A<6>  = 2  ;  A<7>  = 7  ;  A<8>  = 4
        BSORT(A, 1, 8)
        OUT    =
        I      = 0
PLOOP   I      = I + 1  GT(I, 8)            :S(DONE)
        OUT    = IDENT(OUT) A<I>             :S(PLOOP)
        OUT    = OUT ' ' A<I>               :(PLOOP)
DONE    OUTPUT = OUT
END
```

```Icon
# SCRIP DEMO8 -- Sort 8 integers (Icon section)
# Idiom: insertion sort via list subscript shifts
procedure isort(a)
    every i := 2 to *a do {
        v := a[i]
        j := i - 1
        while j >= 1 & a[j] > v do {
            a[j + 1] := a[j]
            j -:= 1
        }
        a[j + 1] := v
    }
    return a
end

procedure main()
    a := [5, 3, 8, 1, 9, 2, 7, 4]
    isort(a)
    out := ""
    every x := !a do
        out ||:= (if *out = 0 then "" else " ") || x
    write(out)
end
```

```Prolog
% SCRIP DEMO8 -- Sort 8 integers (Prolog section)
% Idiom: msort/2 built-in; atomic_list_concat for output
:- initialization(main, main).

main :-
    msort([5, 3, 8, 1, 9, 2, 7, 4], Sorted),
    atomic_list_concat(Sorted, ' ', Out),
    write(Out), nl.
```
