#ifndef _migrator_h_
#define _migrator_h_

typedef struct {
  char *ext;
  float compr;
} ratio;


#define BLOCK_SIZE    1024
#define CLUSTER_BYTES (32*BLOCK_SIZE)

#define SMALL_BYTES          (64.0*1024)
#define MEDIUM_BYTES        (256.0*1024)
#define LARGE_BYTES      (1.0*1024*1024)
#define HUGE_BYTES      (10.0*1024*1024)

#define BYTE_CUTOFF   BLOCK_SIZE
#define AGE_CUTOFF    0.3

#define TIME_CONST    0.40
#define BYTE_CONST    0.001
#define WORTH_CUTOFF  0.2

#define MIGRATOR_VERSION "0.1"

#endif
