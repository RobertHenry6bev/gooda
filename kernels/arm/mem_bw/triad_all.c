/*
Copyright 2012 Google Inc. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
 */

#include <string.h>

#include "arch.h"

size_t triad_copy_vec(size_t len, double xx, double *a, double *b, double *c) {
  size_t bytes = 2*sizeof(double);
  for (size_t i = 0; i < len; i++) {
    a[i] = b[i];
  }
  return bytes;
}

size_t triad_mem_cpy(size_t len, double xx, double *a, double *b, double *c) {
  size_t bytes = 2*sizeof(double);
  memcpy((void *)b, (void *)a, len * sizeof(double));
  return bytes;
}

size_t triad_mem_set(size_t len, double xx, double *a, double *b, double *c) {
  size_t bytes = 1*sizeof(double);
  memset((void *)a, 0, len * sizeof(double));
  return bytes;
}

size_t triad_aligned(size_t len, double xx, double *a, double *b, double *c) {
  size_t bytes = 3*sizeof(double);
  a = __builtin_assume_aligned(a, 64);
  b = __builtin_assume_aligned(b, 64);
  c = __builtin_assume_aligned(c, 64);
  for (size_t i = 0; i < len; i++) {
    a[i] = b[i] + xx * c[i];
  }
  return bytes;
}

size_t triad_aligned_restrict(size_t len, double xx, double *restrict a,
                           double *restrict b, double *restrict c) {
  size_t bytes = 3*sizeof(double);
  a = __builtin_assume_aligned(a, 64);
  b = __builtin_assume_aligned(b, 64);
  c = __builtin_assume_aligned(c, 64);
  for (size_t i = 0; i < len; i++) {
    a[i] = b[i] + xx * c[i];
  }
  return bytes;
}

size_t triad(size_t len, double xx, double *a, double *b, double *c) {
  size_t bytes = 3*sizeof(double);
  for (size_t i = 0; i < len; i++) {
    a[i] = b[i] + xx * c[i];
  }
  return bytes;
}

__attribute__((always_inline))
inline double triad_base(double b, double c, double x) {
  return b + x * c;
}

size_t triad_ini(size_t len, double xx, double *a, double *b, double *c) {
  size_t bytes = 3*sizeof(double);
  for (size_t i = 0; i < len; i++) {
    a[i] = triad_base(b[i], c[i], xx);
  }
  return bytes;
}

size_t triad_restrict(size_t len, double xx,
  double *restrict a, double *restrict b, double *restrict c) {
  size_t bytes = 3*sizeof(double);
  for (size_t i = 0; i < len; i++) {
    a[i] = b[i] + xx * c[i];
  }
  return bytes;
}

size_t triad_vec(size_t len, double xx, double *a, double *b, double *c) {
  size_t bytes = 3*sizeof(double);
#pragma vector aligned
#pragma vector nontemporal
  for (size_t i = 0; i < len; i++) {
    a[i] = b[i] + xx * c[i];
  }
  return bytes;
}

size_t triad_writer10(size_t len, double xx, double *a, double *b, double *c) {
  size_t bytes = 1*sizeof(double);
  for (size_t i = 0; i < len; i++) {
    a[i] = 10.0;
  }
  return bytes;
}

size_t triad_writer00(size_t len, double xx, double *a, double *b, double *c) {
  size_t bytes = 1*sizeof(double);
  for (size_t i = 0; i < len; i++) {
    a[i] = 0; // should get converted into a single memset
  }
  return bytes;
}
