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

#include <asm/mman.h>
#include <err.h>
#include <fcntl.h>
#include <getopt.h>
#include <malloc.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <syscall.h>
#include <time.h>
#include <unistd.h>

#include "arch.h"
#include "triad_all.c"

#define KIND "triad"

typedef unsigned long long uint64_t;
#define DECLARE_ARGS(val, low, high) unsigned low, high
#define isb() asm volatile("isb" : : : "memory")

static inline uint64_t _rdtsc(void) {
  uint64_t cval;
  isb();
  asm volatile("mrs %0, cntvct_el0" : "=r"(cval));
  return cval;
}

static inline uint64_t _loc_freq(void) {
  uint64_t cval;
  isb();
  asm volatile("mrs %0, cntfrq_el0" : "=r"(cval));
  return cval;
}

#define MAX_CPUS 2048
#define NR_CPU_BITS (MAX_CPUS >> 3)
static int pin_cpu(pid_t pid, unsigned int cpu) {
  unsigned long long my_mask[NR_CPU_BITS];

  memset(my_mask, 0, sizeof(my_mask));

  if (cpu >= MAX_CPUS)
    errx(1, "this program supports only up to %d CPUs", MAX_CPUS);

  my_mask[cpu >> 6] = 1ULL << (cpu & 63);

  return syscall(__NR_sched_setaffinity, pid, sizeof(my_mask), &my_mask);
}

void usage() {
  fprintf(stderr,
      "%s need at least 3 arguments setting values for -i, -r and -l\n", KIND);
  fprintf(stderr, " -i indicates what core to use when initializing buffers\n");
  fprintf(stderr, " -r indicates what core to use when running the %s\n", KIND);
  fprintf(stderr, " -l value 0-3 determining the buffer size and thereby "
                  "setting the cache locality of the data\n");
  fprintf(stderr, " [-m] value increases the number of calls to %s by a "
                  "multiplier = value\n", KIND);
  fprintf(stderr, " [-a] offset in bytes to use for base address of buffer a "
                  "(write target)\n");
  fprintf(stderr, " [-b] offset in bytes to use for base address of buffer b "
                  "(read target1)\n");
  fprintf(stderr, " [-c] offset in bytes to use for base address of buffer c "
                  "(read target2)\n");
}

const char *savestr(const char *str) {
    size_t len = strlen(str);
    char *xnew = (char *)calloc(len+1, sizeof(char));
    strcpy(xnew, str);
    return xnew;
}

int main(int argc, char **argv) {
  int mem_level;
  int cpu;
  int cpu_run;
  int bytes_per;
  int scale;

  int offset_a = 0;
  int offset_b = 0;
  int offset_c = 0;
  int mult = 1;
  int iter = 5;  // TODO: originally 100
  int c_val;

  double *a;
  double *b;
  double *c;

  double xx = 0.01;

  double bw;
  double avg_bw;
  double best_bw = -1.0;

  char *buf1;
  char *buf2;
  char *buf3;
  int i;
  int j;
  int k;

  size_t len;
  size_t level_size[4];

  __pid_t pid = 0;

  int cpu_setsize;
  cpu_set_t mask;

  size_t start;
  size_t stop;
  size_t run_time;
  size_t call_start;
  size_t call_stop;
  size_t call_run_time;
  size_t total_bytes = 0;

  size_t sum_run_time = 0;
  size_t run_time_k = 0;
  size_t init_start;
  size_t init_end;
  size_t init_run;

  struct timespec start_t;
  struct timespec stop_t;
  struct timeval start_time;
  struct timeval stop_time;

  size_t total = 0;
  int ret_int, fd = -1;
  size_t gotten_time;
  size_t freq_val;
  off_t offset = 0;
  size_t buf_size;

  const char *output_file_name = "gooda.out";

  if (argc < 3) {
    fprintf(stderr,
      "%s driver needs at least 3 arguments, cpu_init, cpu_run, "
       "cache_level, [call count multiplier  def = 1], [offset a, "
       "offset_b, offset_c  defaults = 0] \n",
       KIND);
    fprintf(stderr, " argc = %d\n", argc);
    usage();
    err(1, "bad arguments");
  }

  len = L4;
  while ((c_val = getopt(argc, argv, "i:r:l:m:a:b:c:o:")) != -1) {
    switch (c_val) {
    case 'i':
      cpu = atoi(optarg);
      break;
    case 'r':
      cpu_run = atoi(optarg);
      break;
    case 'l':
      mem_level = atoi(optarg);
      break;
    case 'm':
      mult = atoi(optarg);
      break;
    case 'a':
      offset_a = atoi(optarg);
      break;
    case 'b':
      offset_b = atoi(optarg);
      break;
    case 'c':
      offset_c = atoi(optarg);
      break;
    case 'o':
      output_file_name = savestr(optarg);
      break;
    default:
      err(1, "unknown option %c", c_val);
    }
  }
  iter = iter * mult;

  // pin core affinity for initialization
  if (pin_cpu(pid, cpu) == -1) {
    err(1, "failed to set affinity");
  } else {
    fprintf(stderr, "process pinned to core %5d for %s init\n", cpu, KIND);
  }

  // set buffer sizes and loop tripcounts based on memory level
  level_size[0] = L1;
  level_size[1] = L2;
  level_size[2] = L3;
  level_size[3] = L4;
  fprintf(stderr,
    "len = %10zd, mem_level = %1d, iter = %5d, mult = %5d\n",
    len, mem_level, iter, mult);

  len = level_size[mem_level] / (size_t)8;
  fprintf(stderr,
    "len = %10zd, mem_level = %1d, iter = %5d, mult = %5d\n",
    len, mem_level, iter, mult);

  scale = level_size[3] / ((size_t)8 * len);
  fprintf(stderr,
    "len = %10zd, mem_level = %1d, iter = %5d, mult = %5d, scale = %7d\n",
    len, mem_level, iter, mult, scale);

  iter = iter * scale * mult;
  fprintf(stderr,
    "len = %10zd, mem_level = %1d, iter = %5d, mult = %5d\n",
    len, mem_level, iter, mult);

  buf_size = sizeof(double) * len;
  buf1 = (char *)mmap(NULL, buf_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANON, fd, offset);
  buf2 = (char *)mmap(NULL, buf_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANON, fd, offset);
  buf3 = (char *)mmap(NULL, buf_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANON, fd, offset);

  a = (double *)buf1;
  b = (double *)buf2;
  c = (double *)buf3;

  for (i = 0; i < len; i++) {
    a[i] = 0.0;
    b[i] = 10.0;
    c[i] = 10.0;
  }

  //
  // pin core affinity for experiment run
  //
  if (pin_cpu(pid, cpu_run) == -1) {
    err(1, "failed to set affinity");
  } else {
    fprintf(stderr, "process pinned to core %5d for %s run\n", cpu_run, KIND);
  }

  // run the test
  fprintf(stdout, "calling %s %5d times with len = %10zd\n", KIND, iter, len);
  ret_int = gettimeofday(&start_time, NULL);
  call_start = _rdtsc();
  for (i = 0; i < iter; i++) {
    start = _rdtsc();
    bytes_per = triad(len, xx, a, b, c);
    stop = _rdtsc();
    run_time = stop - start;
    xx += 0.01;
    total_bytes += len * bytes_per;
    bw = (double)(len * bytes_per) / (double)run_time;
    if (bw > best_bw) {
      best_bw = bw;
    }
  }
  call_stop = _rdtsc();
  freq_val = _loc_freq();
  ret_int = gettimeofday(&stop_time, NULL);
  gotten_time = (size_t)(stop_time.tv_sec - start_time.tv_sec) * 1000000;
  gotten_time += (size_t)(stop_time.tv_usec - start_time.tv_usec);

  call_run_time = call_stop - call_start;
  avg_bw = (double)(total_bytes) / (double)call_run_time;

  FILE *output_file = NULL;
  if (output_file_name[0] != '-') {
    output_file = fopen(output_file_name, "w");
  } else {
    output_file = stdout;
  }
  if (output_file == NULL) {
    fprintf(stderr, "%s: file open problems\n", output_file_name);
  }

  fprintf(output_file,
    "transfering %10zd bytes from memory level %d took %zd cycles/call and "
     "a total of %10zd\n",
     total_bytes, mem_level, run_time, call_run_time);
  fprintf(output_file, "run time = %10zd\n", call_run_time);
  fprintf(output_file, "gettimeofday_run time = %10zd\n", gotten_time);
  fprintf(output_file, "frequency = %10zd\n", freq_val);
  fprintf(output_file, "average bytes/cycle = %f\n", avg_bw);
  fprintf(output_file, "best bytes/cycle = %f\n", best_bw);

  if (output_file != stdout) {
    fclose(output_file); output_file = NULL;
  }

  return 0;
}
