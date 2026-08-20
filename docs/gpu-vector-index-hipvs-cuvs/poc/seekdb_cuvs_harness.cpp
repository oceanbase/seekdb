// Harness: drives seekdb's REAL vector adaptor (oceanbase::common::obvsag) end
// to end. It explicitly marks the handle as lib=cuvs before routing to hipVS.
#include "ob_vsag_adaptor.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cfloat>
#include <vector>
using namespace oceanbase::common::obvsag;

static bool load(const char* path, void* buf, size_t bytes){
  FILE* f=fopen(path,"rb"); if(!f){fprintf(stderr,"open %s fail\n",path);return false;}
  size_t r=fread(buf,1,bytes,f); fclose(f);
  if(r!=bytes){fprintf(stderr,"read %s: got %zu want %zu\n",path,r,bytes);return false;}
  return true;
}

int main(void){
  const int n=10000, dim=128, nq=100, topk=10;
  std::vector<float> base((size_t)n*dim), query((size_t)nq*dim);
  std::vector<int32_t> gt((size_t)nq*topk);
  std::vector<int64_t> ids(n);
  for(int i=0;i<n;i++) ids[i]=i;
  if(!load("/work/datasets/base.f32", base.data(), sizeof(float)*base.size())) return 2;
  if(!load("/work/datasets/query.f32", query.data(), sizeof(float)*query.size())) return 2;
  if(!load("/work/datasets/gt_100x10.i32", gt.data(), sizeof(int32_t)*gt.size())) return 2;

  printf("[harness] backend=cuVS-GPU(hipVS) n=%d dim=%d nq=%d topk=%d\n", n, dim, nq, topk);

  VectorIndexPtr h=nullptr;
  int ret=create_index(h, HNSW_TYPE, "float32", "l2", dim, 16, 200, 200, nullptr);
  printf("[harness] create_index ret=%d handle=%p\n", ret, h);
  if(ret!=0 || h==nullptr) return 3;
  mark_cuvs_index(h);

  ret=build_index(h, base.data(), ids.data(), dim, n);
  printf("[harness] build_index ret=%d\n", ret);
  if(ret!=0) return 4;

  long hit=0, checked=0;
  for(int i=0;i<nq;i++){
    const float* dist=nullptr; const int64_t* out=nullptr; int64_t rsize=0; const char* extra=nullptr;
    ret=knn_search(h, query.data()+(size_t)i*dim, dim, topk, dist, out, rsize, 200, false, extra);
    if(ret!=0){ printf("[harness] knn_search q%d ret=%d\n", i, ret); return 5; }
    for(int k=0;k<rsize && k<topk;k++){ int64_t id=out[k]; checked++;
      for(int j=0;j<topk;j++) if(id==(int64_t)gt[(size_t)i*topk+j]){hit++;break;} }
    if(i==0){ printf("[harness] q0 rsize=%ld top5:", (long)rsize);
      for(int k=0;k<5 && k<rsize;k++) printf(" %ld",(long)out[k]);
      printf("  gt:"); for(int j=0;j<5;j++) printf(" %d", gt[j]); printf("\n"); }
  }
  printf("[harness] recall@%d = %.4f (%ld/%d)\n", topk, (double)hit/((double)nq*topk), hit, nq*topk);
  delete_index(h);
  return 0;
}
