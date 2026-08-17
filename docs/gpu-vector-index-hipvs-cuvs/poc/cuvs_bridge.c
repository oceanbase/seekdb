/* L1 bridge: GPU ANN over seekdb's vectors via hipVS (cuVS) CAGRA.
 * Reads raw float32 base/query, builds CAGRA on GPU, searches top-k,
 * writes neighbor ids (uint32) to out file. Prints build/search timing.
 * Usage: cuvs_bridge <base.f32> <query.f32> <out.u32> N dim Q topk
 */
#include <cuvs/core/c_api.h>
#include <cuvs/neighbors/cagra.h>
#include <dlpack/dlpack.h>
#ifdef __HIP_PLATFORM_AMD__
#include <cuvs/cuda_runtime.h>
#else
#include <cuda_runtime.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define CHECK_CUVS(call) do { cuvsError_t _e=(call); if(_e!=CUVS_SUCCESS){ \
  fprintf(stderr,"CUVS error at %s:%d: %s\n",__FILE__,__LINE__,cuvsGetLastErrorText()); exit(1);} } while(0)
#define CHECK_CUDA(call) do { cudaError_t _e=(call); if(_e!=cudaSuccess){ \
  fprintf(stderr,"CUDA error %d at %s:%d\n",(int)_e,__FILE__,__LINE__); exit(1);} } while(0)

static double now_s(){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec + t.tv_nsec*1e-9; }

static float* read_f32(const char* path, long count){
  FILE* f=fopen(path,"rb"); if(!f){perror(path);exit(1);}
  float* p=(float*)malloc(sizeof(float)*count);
  if(fread(p,sizeof(float),count,f)!=(size_t)count){fprintf(stderr,"short read %s\n",path);exit(1);}
  fclose(f); return p;
}

int main(int argc,char**argv){
  if(argc!=8){fprintf(stderr,"usage: %s base.f32 query.f32 out.u32 N dim Q topk\n",argv[0]);return 2;}
  const char* base_p=argv[1]; const char* query_p=argv[2]; const char* out_p=argv[3];
  int64_t N=atoll(argv[4]), dim=atoll(argv[5]), Q=atoll(argv[6]), topk=atoll(argv[7]);
  float* base=read_f32(base_p, N*dim);
  float* query=read_f32(query_p, Q*dim);

  cuvsResources_t res; CHECK_CUVS(cuvsResourcesCreate(&res));

  DLManagedTensor dset; dset.dl_tensor.data=base;
  dset.dl_tensor.device.device_type=kDLCPU; dset.dl_tensor.ndim=2;
  dset.dl_tensor.dtype.code=kDLFloat; dset.dl_tensor.dtype.bits=32; dset.dl_tensor.dtype.lanes=1;
  int64_t dshape[2]={N,dim}; dset.dl_tensor.shape=dshape; dset.dl_tensor.strides=NULL;

  cuvsCagraIndexParams_t ip; CHECK_CUVS(cuvsCagraIndexParamsCreate(&ip));
  cuvsCagraIndex_t idx; CHECK_CUVS(cuvsCagraIndexCreate(&idx));
  double t0=now_s();
  CHECK_CUVS(cuvsCagraBuild(res, ip, &dset, idx));
  CHECK_CUDA(cudaDeviceSynchronize());
  double build_s=now_s()-t0;

  float* query_d; uint32_t* nbr_d; float* dist_d;
  CHECK_CUVS(cuvsRMMAlloc(res,(void**)&query_d,sizeof(float)*Q*dim));
  CHECK_CUVS(cuvsRMMAlloc(res,(void**)&nbr_d,sizeof(uint32_t)*Q*topk));
  CHECK_CUVS(cuvsRMMAlloc(res,(void**)&dist_d,sizeof(float)*Q*topk));
  CHECK_CUDA(cudaMemcpy(query_d,query,sizeof(float)*Q*dim,cudaMemcpyDefault));

  DLManagedTensor qt; qt.dl_tensor.data=query_d; qt.dl_tensor.device.device_type=kDLCUDA;
  qt.dl_tensor.ndim=2; qt.dl_tensor.dtype.code=kDLFloat; qt.dl_tensor.dtype.bits=32; qt.dl_tensor.dtype.lanes=1;
  int64_t qshape[2]={Q,dim}; qt.dl_tensor.shape=qshape; qt.dl_tensor.strides=NULL;

  DLManagedTensor nt; nt.dl_tensor.data=nbr_d; nt.dl_tensor.device.device_type=kDLCUDA;
  nt.dl_tensor.ndim=2; nt.dl_tensor.dtype.code=kDLUInt; nt.dl_tensor.dtype.bits=32; nt.dl_tensor.dtype.lanes=1;
  int64_t nshape[2]={Q,topk}; nt.dl_tensor.shape=nshape; nt.dl_tensor.strides=NULL;

  DLManagedTensor dt; dt.dl_tensor.data=dist_d; dt.dl_tensor.device.device_type=kDLCUDA;
  dt.dl_tensor.ndim=2; dt.dl_tensor.dtype.code=kDLFloat; dt.dl_tensor.dtype.bits=32; dt.dl_tensor.dtype.lanes=1;
  int64_t dshape2[2]={Q,topk}; dt.dl_tensor.shape=dshape2; dt.dl_tensor.strides=NULL;

  cuvsCagraSearchParams_t sp; CHECK_CUVS(cuvsCagraSearchParamsCreate(&sp));
  cuvsFilter filter; filter.type=NO_FILTER; filter.addr=(uintptr_t)NULL;
  /* warm-up + timed search */
  CHECK_CUVS(cuvsCagraSearch(res,sp,idx,&qt,&nt,&dt,filter));
  CHECK_CUDA(cudaDeviceSynchronize());
  t0=now_s();
  CHECK_CUVS(cuvsCagraSearch(res,sp,idx,&qt,&nt,&dt,filter));
  CHECK_CUDA(cudaDeviceSynchronize());
  double search_s=now_s()-t0;

  uint32_t* nbr_h=(uint32_t*)malloc(sizeof(uint32_t)*Q*topk);
  CHECK_CUDA(cudaMemcpy(nbr_h,nbr_d,sizeof(uint32_t)*Q*topk,cudaMemcpyDefault));
  FILE* of=fopen(out_p,"wb"); fwrite(nbr_h,sizeof(uint32_t),Q*topk,of); fclose(of);

  printf("CAGRA build_s=%.4f search_s=%.4f (%.3f ms/query, %.0f qps) N=%ld dim=%ld Q=%ld topk=%ld\n",
         build_s, search_s, search_s*1000.0/Q, Q/search_s, (long)N,(long)dim,(long)Q,(long)topk);

  free(nbr_h);
  CHECK_CUVS(cuvsCagraSearchParamsDestroy(sp));
  CHECK_CUVS(cuvsRMMFree(res,dist_d,sizeof(float)*Q*topk));
  CHECK_CUVS(cuvsRMMFree(res,nbr_d,sizeof(uint32_t)*Q*topk));
  CHECK_CUVS(cuvsRMMFree(res,query_d,sizeof(float)*Q*dim));
  CHECK_CUVS(cuvsCagraIndexDestroy(idx));
  CHECK_CUVS(cuvsCagraIndexParamsDestroy(ip));
  CHECK_CUVS(cuvsResourcesDestroy(res));
  free(base); free(query);
  return 0;
}
