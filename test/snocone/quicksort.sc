// quicksort.sc — recursive quicksort (SC-16)
// Note: Snocone arrays pass by reference (descriptor sharing), so in-place sort works.
// Validated by checking sorted output directly.

procedure QSort(arr, lo, hi, pivot, i, j, tmp) {
    if (GE(lo, hi)) { return; }
    pivot = arr[lo + REMDR(hi - lo, 2)];
    i = lo; j = hi;
    while (LE(i, j)) {
        while (LT(arr[i], pivot)) { i = i + 1; }
        while (GT(arr[j], pivot)) { j = j - 1; }
        if (LE(i, j)) {
            tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp;
            i = i + 1; j = j - 1;
        }
    }
    QSort(arr, lo, j);
    QSort(arr, i, hi);
}

a = ARRAY(8);
a[1] = 5; a[2] = 3; a[3] = 8; a[4] = 1;
a[5] = 9; a[6] = 2; a[7] = 7; a[8] = 4;
QSort(a, 1, 8);
i = 1;
while (LE(i, 8)) { OUTPUT = a[i]; i = i + 1; }
