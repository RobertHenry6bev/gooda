#include <string.h>

#define DIM 8192000

int triad_copy_vec(int len, double xx, double *a, double *b, double *c) {
  int i, bytes = 16;
  for (i = 0; i < len; i++) {
    a[i] = b[i];
  }
  return bytes;
}

int triad_mem_cpy(int len, double xx, double *a, double *b, double *c) {
  int i, bytes = 16;

  memcpy((void *)b, (void *)a, len * sizeof(double));
  return bytes;
}

int triad_mem_set(int len, double xx, double *a, double *b, double *c) {
  int i = 0, bytes = 8;

  memset((void *)a, i, len * sizeof(double));
  return bytes;
}

int triad_aligned(int len, double xx, double *a, double *b, double *c) {

  int i, bytes = 24;
  a = __builtin_assume_aligned(a, 64);
  b = __builtin_assume_aligned(b, 64);
  c = __builtin_assume_aligned(c, 64);
  for (i = 0; i < len; i++) {
    a[i] = b[i] + xx * c[i];
  }
  return bytes;
}

int triad_aligned_restrict(int len, double xx, double *restrict a,
                           double *restrict b, double *restrict c) {

  int i, bytes = 24;
  a = __builtin_assume_aligned(a, 64);
  b = __builtin_assume_aligned(b, 64);
  c = __builtin_assume_aligned(c, 64);
  for (i = 0; i < len; i++) {
    a[i] = b[i] + xx * c[i];
  }
  return bytes;
}

int triad(int len, double xx, double *a, double *b, double *c) {
  int i, bytes = 24;
  for (i = 0; i < len; i++) {
    a[i] = b[i] + xx * c[i];
  }
  return bytes;
}

__attribute__((always_inline)) inline double triad_base(double b, double c, double x) {
  return b + x * c;
}

int triad_ini(int len, double xx, double *a, double *b, double *c) {
  int i, bytes = 24;
  for (i = 0; i < len; i++) {
    a[i] = triad_base(b[i], c[i], xx);
  }
  return bytes;
}

int triad_restrict(int len, double xx, double *restrict a, double *restrict b,
                   double *restrict c) {
  int i, bytes = 24;
  for (i = 0; i < len; i++) {
    a[i] = b[i] + xx * c[i];
  }
  return bytes;
}

int triad_vec(int len, double xx, double *a, double *b, double *c) {
  int i, bytes = 24;
#pragma vector aligned
#pragma vector nontemporal
  for (i = 0; i < len; i++) {
    a[i] = b[i] + xx * c[i];
  }
  return bytes;
}

int triad_writer10(int len, double xx, double *a, double *b, double *c) {
  int i, bytes = 24;
  for (i = 0; i < len; i++) {
    a[i] = 10.0;
  }
  return bytes;
}

int triad_writer00(int len, double xx, double *a, double *b, double *c) {
  int i, bytes = 24;
  for (i = 0; i < len; i++) {
    a[i] = 0;
  }
  return bytes;
}
