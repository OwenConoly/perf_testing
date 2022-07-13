/*
 * Benchmarking utilities extracted from SUPERCOP by Andres Erbsen
 * based on measure-anything.c version 20120328 and measure.c
 * by D. J. Bernstein
 * Public domain.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#include <math.h>
#include "my_implementation.h"
#include "assembly_implementation.h"

typedef void (*function_type)(uint64_t*, const uint64_t*, const uint64_t*);

// cpucycles_amd64cpuinfo
long long cpucycles(void)
{
  unsigned long long result;
  asm volatile(".byte 15;.byte 49;shlq $32,%%rdx;orq %%rdx,%%rax"
    : "=a" (result) ::  "%rdx");
  return result;
}

// SUPERCOP randombytes
static uint32_t seed[32] = { 3,1,4,1,5,9,2,6,5,3,5,8,9,7,9,3,2,3,8,4,6,2,6,4,3,3,8,3,2,7,9,5 } ;
static uint32_t in[12];
static uint32_t out[8];
static int outleft = 0;
static void surf(void)
{
  #define ROTATE(x,b) (((x) << (b)) | ((x) >> (32 - (b))))
  #define MUSH(i,b) x = t[i] += (((x ^ seed[i]) + sum) ^ ROTATE(x,b));
  uint32_t t[12]; uint32_t x; uint32_t sum = 0;
  int r; int i; int loop;

  for (i = 0;i < 12;++i) t[i] = in[i] ^ seed[12 + i];
  for (i = 0;i < 8;++i) out[i] = seed[24 + i];
  x = t[11];
  for (loop = 0;loop < 2;++loop) {
    for (r = 0;r < 16;++r) {
      sum += 0x9e3779b9;
      MUSH(0,5) MUSH(1,7) MUSH(2,9) MUSH(3,13)
      MUSH(4,5) MUSH(5,7) MUSH(6,9) MUSH(7,13)
      MUSH(8,5) MUSH(9,7) MUSH(10,9) MUSH(11,13)
    }
    for (i = 0;i < 8;++i) out[i] ^= t[i + 4];
  }
  #undef ROTATE
  #undef MUSH
}
void randombytes(unsigned char *x,unsigned long long xlen)
{
  while (xlen > 0) {
    if (!outleft) {
      if (!++in[0]) if (!++in[1]) if (!++in[2]) ++in[3];
      surf();
      outleft = 8;
    }
    *x = out[--outleft];
    ++x;
    --xlen;
  }
}

// SUPERCOP limits
void limits()
{
#ifdef RLIM_INFINITY
  struct rlimit r;
  r.rlim_cur = 0;
  r.rlim_max = 0;
#ifdef RLIMIT_NOFILE
  setrlimit(RLIMIT_NOFILE,&r);
#endif
#ifdef RLIMIT_NPROC
  setrlimit(RLIMIT_NPROC,&r);
#endif
#ifdef RLIMIT_CORE
  setrlimit(RLIMIT_CORE,&r);
#endif
#endif
}


// place the call to UUT in a separate function, because gcc prohibits
// use of bp in inline assembly if you have a variable-length array or
// alloca in the code; see
// https://stackoverflow.com/questions/46248430/how-can-i-determine-preferably-at-compile-time-whether-gcc-is-using-rbp-based?noredirect=1#comment79462384_46248430
void __attribute__ ((noinline)) measure_helper(int n, unsigned char *buf, long long* cycles0, long long* cycles1, int limbs, function_type UUT)
{
  uint64_t * numbers = (uint64_t*) buf;

  uint64_t* input1;
  uint64_t* input2;
  uint64_t* output;
  long long cycles_0;
  long long cycles_1;

  for (int i = 0;i < n;++i) {
    input1 = numbers + (3 * limbs) * i;
    input2 = numbers + (3 * limbs) * i + 8;
    output = numbers + (3 * limbs) * i + 16;
    
    cycles0[i] = cpucycles();


    UUT(output, input1, input2);


    cycles1[i] = cpucycles();

    //printf("cycles: %lld\n", cycles1[i] - cycles0[i]);
  }
}


void measure(int n, int limbs, int random, function_type UUT)
{
  // alignment is 8 because I want these to be uint64s.  limbs * 8 is the size of one number.  Multiply by three because there are 
  // two inputs and an output.
  unsigned char *buf = aligned_alloc(8, (limbs * 8) * 3 * n + (1024 * 8));
  
  if(random)
    randombytes(buf, (limbs * 8) * n * 3);

  long long* cycles0 = calloc(n, sizeof(long long));
  long long* cycles1 = calloc(n, sizeof(long long));
  long long* cycles = calloc(n, sizeof(long long));

  measure_helper(n, buf, cycles0, cycles1, limbs, UUT);

  for(int i = 0; i < n; ++i)
    cycles[i] = cycles1[i] - cycles0[i];

  float mean = 0.0;
  for(int i = 0; i < n; ++i)
    mean += (float) cycles[i];

  mean /= n;
  float sd = 0.0;
  for(int i = 0; i < n; ++i)
    sd += (cycles[i] - mean) * (cycles[i] - mean);
  sd  = sqrt(sd / n);
  
  printf("trials: %d\naverage cycles per trial: %f\nstandard deviation: %f\n", n, mean, sd);

  //for (int i = 0;i < n;++i) printf("%lld\n", cycles[i]);

    __asm__ __volatile__("" :: "m" (buf)); // do not optimize buf away
  free(cycles);
}


int main(int argc, char** argv)
{
  sleep(1);
  int n = 0;
  int limbs = 0;
  int random = 0;
  function_type f = bitcoin_mul_u64;
  if (argc != 4) return 111;
  sscanf(argv[1], "%d", &n);
  int length = strlen(argv[2]);
  char* f_string = argv[2];
  sscanf(argv[3], "%d", &random);
  limits();


  if(strcmp(f_string, "my_imp") == 0){
    limbs = 5;
    f = bitcoin_mul_u64;
  }
  else if(strcmp(f_string, "assembly_imp") == 0){
    limbs = 5;
    f = secp256k1_fe_mul_inner;
  }
  else{
    return 1;
  }

  measure(n, limbs, random, f);
  return 0;
}
