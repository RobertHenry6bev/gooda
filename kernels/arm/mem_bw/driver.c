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
#include <inttypes.h>
#include <malloc.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "arch.h"
#include "triad_all.c"
#include "../matrix_mult/matrix_mult_all.c"

#include "librdtsc/rdtsc.h"

typedef size_t (*triad_func)(size_t len, double xx, double *a, double *b, double *c);
struct triad_namelist_t {
  const char *name;
  triad_func func;
};
static struct triad_namelist_t triad_namelist[] = {
  {"copy_vec", triad_copy_vec},
  {"mem_cpy", triad_mem_cpy},
  {"mem_set", triad_mem_set},
  {"aligned", triad_aligned},
  {"aligned_restrict", triad_aligned_restrict},
  {"triad", triad},
  {"ini", triad_ini},
  {"restrict", triad_restrict},
  {"vec", triad_vec},
  {"writer10", triad_writer10},
  {"writer00", triad_writer00},
  {0, 0},
};

typedef size_t (*matrix_mult_func)(size_t len, double *restrict a, double *restrict b, double *restrict c);
struct matrix_mult_namelist_t {
  const char *name;
  matrix_mult_func func;
};
static struct matrix_mult_namelist_t matrix_mult_namelist[] = {
  {"matrix_mult_basic", matrix_mult_basic},
  {"matrix_mult_inter", matrix_mult_inter},
  {"matrix_mult_trans_by4", matrix_mult_trans_by4},
  {"matrix_mult_trans", matrix_mult_trans},
  {"matrix_mult_outer2by2", matrix_mult_outer2by2},
  {0,0},
};

#if defined(__x86_64__) // {

#define DECLARE_ARGS(val, low, high)    unsigned low, high
#define EAX_EDX_VAL(val, low, high)     ((low) | ((uint64_t)(high) << 32))
#define EAX_EDX_ARGS(val, low, high)    "a" (low), "d" (high)
#define EAX_EDX_RET(val, low, high)     "=a" (low), "=d" (high)
static inline uint64_t _original_rdtsc(void)
{
  DECLARE_ARGS(val, low, high);
  asm volatile("rdtsc" : EAX_EDX_RET(val, low, high));
  return EAX_EDX_VAL(val, low, high);
}
static inline uint64_t _original_loc_freq(void) {
  return 0; // TBD
}
#endif // }

#if defined(__aarch64__)
#define DECLARE_ARGS(val, low, high) unsigned low, high
#define isb() asm volatile("isb" : : : "memory")
static inline uint64_t _original_rdtsc(void) {
  uint64_t cval;
  isb();
  asm volatile("mrs %0, cntvct_el0" : "=r"(cval));
  return cval;
}

static inline uint64_t _original_loc_freq(void) {
  uint64_t cval;
  isb();
  asm volatile("mrs %0, cntfrq_el0" : "=r"(cval));
  return cval;
}
#endif // }

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
      "%s need at least 3 arguments setting values for -i, -r and -l\n", "driver");
  fprintf(stderr, " -i indicates what core to use when initializing buffers\n");
  fprintf(stderr, " -r indicates what core to use when running the %s\n", "driver");
  fprintf(stderr, " -l value 0-3 determining the buffer size and thereby "
                  "setting the cache locality of the data\n");
  fprintf(stderr, " [-m] value increases the number of calls to %s by a "
                  "multiplier = value\n", "driver");
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
  {
    int res = rdtsc_init();
    if (res != 0) {
        fprintf(stderr, "rdtsc_init: ERROR DURING INITIALIZATION!!!\n");
        exit(res);
    }
    // uint64_t tsc_hz = rdtsc_get_tsc_hz();
    // fprintf(stdout, "tsc_MHz = %5.2e\n", tsc_hz / 1.0e6);
  }
  uint64_t freq_val = rdtsc_get_tsc_hz();
  uint64_t assumed_clock_freq = (uint64_t)(2.8e9);
  double freq_scale = (double)(assumed_clock_freq) / (double)(freq_val);

  int mem_level = -1;
  int cpu = -1;
  int cpu_run = -1;
  int scale = -1;

  int offset_a = 0;
  int offset_b = 0;
  int offset_c = 0;
  int mult = 1;
  int iter = 5;  // TODO: originally 100

  int i;
  int k;

  size_t level_size[4];

  __pid_t pid = 0;

  size_t total = 0;
  int ret_int, fd = -1;
  off_t offset = 0;

  const char *output_file_name = "gooda.out";
  const char *test_name = NULL;

  if (argc < 3) {
    fprintf(stderr,
      "%s driver needs at least 3 arguments, cpu_init, cpu_run, "
       "cache_level, [call count multiplier  def = 1], [offset a, "
       "offset_b, offset_c  defaults = 0] \n", "driver");
    fprintf(stderr, " argc = %d\n", argc);
    usage();
    err(1, "bad arguments");
  }

  size_t len = 0;

  int c_val = 0;
  while ((c_val = getopt(argc, argv, "i:r:l:m:a:b:c:o:t:")) != -1) {
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
    case 't':
      test_name = savestr(optarg);
      break;
    default:
      err(1, "unknown option %c", c_val);
      return 1;
    }
  }

  triad_func triad_test_function = NULL;
  matrix_mult_func matrix_mult_test_function = NULL;
  bool is_mem_bw = false;
  {
    if (test_name == NULL) {
      fprintf(stderr, "You must specify a test_name using the -t flag\n");
      exit(1);
    }
    for (int i = 0; triad_namelist[i].name; i++) {
      if (strcmp(triad_namelist[i].name, test_name) == 0) {
        triad_test_function = triad_namelist[i].func;
        break;
      }
    }
    for (int i = 0; matrix_mult_namelist[i].name; i++) {
      if (strcmp(matrix_mult_namelist[i].name, test_name) == 0) {
        matrix_mult_test_function = matrix_mult_namelist[i].func;
        break;
      }
    }
    if (triad_test_function == NULL && matrix_mult_test_function == NULL) {
      fprintf(stderr, "%s: Unknown test function name\n", test_name);
      exit(1);
    }
    is_mem_bw = (triad_test_function != NULL);
  }

  if (len == 0) {
    len = is_mem_bw ? L4 : 2048;  // WTF? TODO
  }
  iter = iter * mult;

  //
  // pin core affinity for initialization
  //
  if (pin_cpu(pid, cpu) == -1) {
    err(1, "failed to set affinity");
  } else {
    fprintf(stderr, "process pinned to core %5d for %s init\n", cpu, test_name);
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

  double *a;
  double *b;
  double *c;
  {
    size_t buf_size = is_mem_bw
      ? sizeof(double) * len
      : ((len + sizeof (double)) * (len + sizeof(double)) * sizeof(double));
    char *buf1 = (char *)mmap(NULL, buf_size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON, fd, offset);
    char *buf2 = (char *)mmap(NULL, buf_size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON, fd, offset);
    char *buf3 = (char *)mmap(NULL, buf_size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON, fd, offset);
    a = (double *)buf1;
    b = (double *)buf2;
    c = (double *)buf3;
    if (is_mem_bw) {
      for (int i = 0; i < len; i++) {
        a[i] = 0.0;
        b[i] = 10.0;
        c[i] = 10.0;
      }
    } else {
       for(i=0; i<len; i++) {
          for(k=0; k<len; k++) {
            a[i*len+k] = 0.0;
            b[i*len+k] = 1.1;
            c[i*len+k] = 1.1;
          }
        }
    }
  }

  //
  // pin core affinity for experiment run
  //
  if (pin_cpu(pid, cpu_run) == -1) {
    err(1, "failed to set affinity");
  } else {
    fprintf(stderr, "process pinned to core %5d for %s run\n", cpu_run, test_name);
  }

  // run the test
  fprintf(stdout, "calling %s %5d times with len = %10zd\n", test_name, iter, len);
  struct timeval start_time;
  ret_int = gettimeofday(&start_time, NULL);

  size_t total_bytes = 0;

  size_t call_start = rdtsc();
  double best_bw = -1.0;  // loop carried
  double xx = 0.01;  // loop carried
  for (i = 0; i < iter; i++) {
    size_t start = rdtsc();
    size_t bytes_per = triad_test_function 
      ? (triad_test_function)(len, xx, a, b, c)
      : (matrix_mult_test_function)(len, a, b, c);
    size_t stop = rdtsc();
    xx += 0.01;
    //
    double run_time = (double)(stop - start) * freq_scale;
    total_bytes += len * bytes_per;
    double bw = (double)(len * bytes_per) / run_time;
    if (false) {
      fprintf(stdout, "i=%8d bw=%8.3f bytes/cycle\n", i, bw);
    }
    if (bw > best_bw) {
      best_bw = bw;
    }
  }
  size_t call_stop = rdtsc();

  struct timeval stop_time;
  ret_int = gettimeofday(&stop_time, NULL);

  size_t gotten_time = 0;
  gotten_time += (size_t)(stop_time.tv_sec - start_time.tv_sec) * 1000000;
  gotten_time += (size_t)(stop_time.tv_usec - start_time.tv_usec);

  double call_run_time = (double)(call_stop - call_start) * freq_scale;
  double avg_bw = (double)(total_bytes) / (double)call_run_time;

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
    "transfering %10zd bytes from memory level %d took a total of %10g cycles\n",
     total_bytes, mem_level, call_run_time);
  fprintf(output_file, "run time = %10g cycles\n", call_run_time);
  fprintf(output_file, "gettimeofday_run time = %10zd\n", gotten_time);
  fprintf(output_file, "frequency real_time_clock = %10zd\n", freq_val);
  fprintf(output_file, "frequency core assumed = %10zd\n", assumed_clock_freq);
  fprintf(output_file, "average bytes/cycle = %f\n", avg_bw);
  fprintf(output_file, "best bytes/cycle = %f\n", best_bw);

  if (output_file != stdout) {
    fclose(output_file); output_file = NULL;
  }

  return 0;
}
