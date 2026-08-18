// Batch-operator harness: drives seekdb REAL obvsag adaptor. Buffers 10k vectors
// via add_index (the plain-HNSW path), then compares the per-query knn_search
// loop (nested-loop / current SQL behavior) against the NEW batch seam
// cuvs_knn_search_batch (nq probes -> ONE GPU call). Same index, same data.
#include "ob_vsag_adaptor.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <ctime>
using namespace oceanbase::common::obvsag;
static double now(){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec/1e9; }
static bool load(const char* p, void* b, size_t n){ FILE* f=fopen(p,"rb"); if(!f){fprintf(stderr,"open %s\n",p);return false;} size_t r=fread(b,1,n,f); fclose(f); return r==n; }
static double recall(const int64_t* ids, const int32_t* gt, int nq, int k){
  long hit=0; for(int q=0;q<nq;q++) for(int i=0;i<k;i++){ int64_t g=ids[(size_t)q*k+i];
    for(int j=0;j<k;j++) if(g==(int64_t)gt[(size_t)q*k+j]){hit++;break;} }
  return (double)hit/((double)nq*k);
}
int main(){
  const int n=10000, dim=128, nq=100, topk=10;
  std::vector<float> base((size_t)n*dim), query((size_t)nq*dim);
  std::vector<int32_t> gt((size_t)nq*topk);
  std::vector<int64_t> ids(n); for(int i=0;i<n;i++) ids[i]=i;
  if(!load("/work/datasets/base.f32",base.data(),sizeof(float)*base.size())) return 2;
  if(!load("/work/datasets/query.f32",query.data(),sizeof(float)*query.size())) return 2;
  if(!load("/work/datasets/gt_100x10.i32",gt.data(),sizeof(int32_t)*gt.size())) return 2;
  const char* env=getenv("OB_VSAG_USE_CUVS");
  printf("[bh] backend=%s n=%d dim=%d nq=%d topk=%d\n",(env&&env[0]==0x31)?"cuVS-GPU":"VSAG-CPU",n,dim,nq,topk);

  VectorIndexPtr h=nullptr;
  if(create_index(h,HNSW_TYPE,"float32","l2",dim,16,200,200,nullptr)!=0||!h){printf("create fail\n");return 3;}
  // Buffer all rows via add_index (plain-HNSW path -> ob_cuvs_add buffers for GPU).
  if(add_index(h,base.data(),ids.data(),dim,n)!=0){printf("add_index fail\n");return 4;}
  printf("[bh] add_index buffered %d rows\n", n);

  // Warm: one knn_search triggers the lazy cuVS CAGRA build.
  { const float* d=nullptr; const int64_t* o=nullptr; int64_t rs=0; const char* ex=nullptr;
    knn_search(h,query.data(),dim,topk,d,o,rs,200,false,ex); }

  // ---- BASELINE: per-query knn_search loop (nested-loop / current SQL path) ----
  std::vector<int64_t> loop_ids((size_t)nq*topk,-1);
  double t0=now();
  for(int i=0;i<nq;i++){ const float* d=nullptr; const int64_t* o=nullptr; int64_t rs=0; const char* ex=nullptr;
    if(knn_search(h,query.data()+(size_t)i*dim,dim,topk,d,o,rs,200,false,ex)!=0){printf("knn %d fail\n",i);return 5;}
    for(int k=0;k<topk;k++) loop_ids[(size_t)i*topk+k]=(k<rs)?o[k]:-1; }
  double t1=now();
  printf("[bh] PER-QUERY knn_search x%d: %.2f ms total, %.3f ms/probe, %.0f probes/s, recall@%d=%.4f\n",
         nq,(t1-t0)*1000,(t1-t0)*1000/nq,nq/(t1-t0),topk,recall(loop_ids.data(),gt.data(),nq,topk));

  // ---- BATCH SEAM: nq probes -> ONE cuVS call ----
  std::vector<int64_t> b_ids((size_t)nq*topk,-1); std::vector<float> b_dist((size_t)nq*topk);
  double b0=now();
  long served=cuvs_knn_search_batch(h, query.data(), nq, topk, b_ids.data(), b_dist.data());
  double b1=now();
  if(served==nq){
    printf("[bh] BATCH cuvs_knn_search_batch(nq=%d): %.3f ms total, %.4f ms/probe, %.0f probes/s, recall@%d=%.4f\n",
           nq,(b1-b0)*1000,(b1-b0)*1000/nq,nq/(b1-b0),topk,recall(b_ids.data(),gt.data(),nq,topk));
    printf("[bh] SPEEDUP (batch vs per-query): %.1fx throughput\n",(t1-t0)/(b1-b0));
    // cross-check batch vs loop agreement
    long agree=0; for(int i=0;i<nq*topk;i++) if(b_ids[i]==loop_ids[i]) agree++;
    printf("[bh] batch-vs-loop identical ids: %ld/%d\n", agree, nq*topk);
  } else {
    printf("[bh] BATCH served=%ld (fell back; need OB_VSAG_USE_CUVS=1 + built index)\n", served);
  }
  delete_index(h);
  return 0;
}
