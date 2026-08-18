/* Batch-size sweep: build CAGRA once (10k), sweep nq (tiled real queries),
 * report per-probe latency + throughput to show the batch ramp. */
#include "seekdb_cuvs_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static double now(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec + t.tv_nsec/1e9; }
static float* load_f32(const char* p, long n){ float* b=malloc(sizeof(float)*n); FILE*f=fopen(p,"rb"); if(!f||fread(b,4,n,f)!=(size_t)n){perror(p);exit(1);} fclose(f); return b; }
int main(void){
  long n=10000, dim=128, topk=10, nqbase=100;
  float* base=load_f32("/work/datasets/base.f32", n*dim);
  float* q100=load_f32("/work/datasets/query.f32", nqbase*dim);
  void* h=seekdb_cuvs_build(base,n,dim); if(!h){printf("build fail\n");return 1;}
  long sizes[]={1,10,50,100,500,1000,2000,5000}; int ns=8;
  long maxnq=5000;
  float* q=malloc(sizeof(float)*maxnq*dim);           /* tiled queries */
  for(long i=0;i<maxnq;i++) for(long d=0;d<dim;d++) q[i*dim+d]=q100[(i%nqbase)*dim+d];
  unsigned* ids=malloc(sizeof(unsigned)*maxnq*topk);
  seekdb_cuvs_search(h,q,1,topk,ids,NULL); /* warm */
  printf("%-8s %-14s %-16s %-12s\n","nq","total_ms","ms/probe","probes/s");
  for(int s=0;s<ns;s++){ long nq=sizes[s];
    double best=9e9; for(int r=0;r<5;r++){ double t=now(); seekdb_cuvs_search(h,q,nq,topk,ids,NULL); double dt=now()-t; if(dt<best)best=dt; }
    printf("%-8ld %-14.3f %-16.5f %-12.0f\n", nq, best*1000, best*1000/nq, nq/best);
  }
  seekdb_cuvs_free(h); return 0;
}
