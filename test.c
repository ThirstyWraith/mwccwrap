/* More complex test for mwccwrap */

static int global_counter = 0;

int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

void increment(void) {
    global_counter++;
}

int get_counter(void) {
    return global_counter;
}

int sum_array(int *arr, int count) {
    int total = 0;
    int i;
    for (i = 0; i < count; i++) {
        total += arr[i];
    }
    return total;
}
