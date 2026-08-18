/* Batch vector-search operator PoC: N probes -> ONE cuVS batch call,
 * vs the per-probe (nested-loop) path. Real data + ground truth. */
#include "seekdb_cuvs_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static double now(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec + t.tv_nsec/1e9; }
static float* load_f32(const char* p, long n){ float* b=malloc(sizeof(float)*n); FILE*f=fopen(p,"rb"); if(!f){perror(p);exit(1);} if(fread(b,4,n,f)!=(size_t)n){fprintf(stderr,"short read %s\n",p);exit(1);} fclose(f); return b; }
static int* load_i32(const char* p, long n){ int* b=malloc(sizeof(int)*n); FILE*f=fopen(p,"rb"); if(!f){perror(p);exit(1);} if(fread(b,4,n,f)!=(size_t)n){fprintf(stderr,"short read %s\n",p);exit(1);} fclose(f); return b; }
static double recall_at_k(const unsigned* ids, const int* gt, int nq, int k){
  long hit=0; for(int q=0;q<nq;q++){ for(int i=0;i<k;i++){ unsigned got=ids[(long)q*k+i];
    for(int j=0;j<k;j++){ if((int)got==gt[(long)q*k+j]){hit++;break;} } } }
  return (double)hit/((double)nq*k);
}
int main(void){
  long n=10000, dim=128, nq=100, topk=10;
  float* base=load_f32("/work/datasets/base.f32", n*dim);
  float* query=load_f32("/work/datasets/query.f32", nq*dim);
  int*  gt   =load_i32("/work/datasets/gt_100x10.i32", nq*topk);
  double bt=now(); void* h=seekdb_cuvs_build(base,n,dim); bt=now()-bt;
  if(!h){printf("build fail\n");return 1;}
  printf("cuVS CAGRA build (10000x128): %.1f ms\n", bt*1000);
  unsigned* ids=malloc(sizeof(unsigned)*nq*topk);
  float* dist=malloc(sizeof(float)*nq*topk);
  seekdb_cuvs_search(h,query,1,topk,ids,dist); /* warm */
  /* ---- PER-PROBE (nested-loop simulation): nq separate calls ---- */
  double t0=now();
  for(int i=0;i<nq;i++) seekdb_cuvs_search(h, query+(long)i*dim, 1, topk, ids+(long)i*topk, dist+(long)i*topk);
  double t1=now();
  double rc_loop=recall_at_k(ids,gt,nq,topk);
  printf("PER-PROBE (%ld x nq=1): %.2f ms total, %.3f ms/probe, %.0f probes/s, recall@10=%.4f\n",
         nq, (t1-t0)*1000, (t1-t0)*1000/nq, nq/(t1-t0), rc_loop);
  /* ---- BATCH operator: all nq probes in ONE call ---- */
  double b0=now(); seekdb_cuvs_search(h, query, nq, topk, ids, dist); double b1=now();
  double rc_batch=recall_at_k(ids,gt,nq,topk);
  printf("BATCH (nq=%ld in 1 call): %.3f ms total, %.4f ms/probe, %.0f probes/s, recall@10=%.4f\n",
         nq, (b1-b0)*1000, (b1-b0)*1000/nq, nq/(b1-b0), rc_batch);
  printf("SPEEDUP (batch vs per-probe): %.1fx throughput\n", (double)(t1-t0)/(b1-b0));
  seekdb_cuvs_free(h); return 0;
}
