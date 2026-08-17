/* seekdb <-> hipVS bridge: exposes a plain-C CAGRA kNN; internally uses libcuvs_c. */
#include "seekdb_cuvs_bridge.h"
#include <cuvs/core/c_api.h>
#include <cuvs/neighbors/cagra.h>
#include <dlpack/dlpack.h>
#ifdef __HIP_PLATFORM_AMD__
#include <cuvs/cuda_runtime.h>
#else
#include <cuda_runtime.h>
#endif
#include <stdio.h>

#define CK(call) do { cuvsError_t _e=(call); if(_e!=CUVS_SUCCESS){ \
  fprintf(stderr,"[seekdb_cuvs] err %s:%d: %s\n",__FILE__,__LINE__,cuvsGetLastErrorText()); return 10;} } while(0)
#define CKC(call) do { cudaError_t _e=(call); if(_e!=cudaSuccess){ \
  fprintf(stderr,"[seekdb_cuvs] cuda err %d %s:%d\n",(int)_e,__FILE__,__LINE__); return 11;} } while(0)

int seekdb_cuvs_cagra_knn(const float* base, long n, long dim,
                          const float* query, long nq, long topk,
                          unsigned int* out_ids)
{
  cuvsResources_t res; CK(cuvsResourcesCreate(&res));
  DLManagedTensor dset; dset.dl_tensor.data=(void*)base;
  dset.dl_tensor.device.device_type=kDLCPU; dset.dl_tensor.ndim=2;
  dset.dl_tensor.dtype.code=kDLFloat; dset.dl_tensor.dtype.bits=32; dset.dl_tensor.dtype.lanes=1;
  long ds[2]={n,dim}; dset.dl_tensor.shape=ds; dset.dl_tensor.strides=0;

  cuvsCagraIndexParams_t ip; CK(cuvsCagraIndexParamsCreate(&ip));
  cuvsCagraIndex_t idx; CK(cuvsCagraIndexCreate(&idx));
  CK(cuvsCagraBuild(res, ip, &dset, idx));

  float* qd; unsigned* nd; float* dd;
  CK(cuvsRMMAlloc(res,(void**)&qd,sizeof(float)*nq*dim));
  CK(cuvsRMMAlloc(res,(void**)&nd,sizeof(unsigned)*nq*topk));
  CK(cuvsRMMAlloc(res,(void**)&dd,sizeof(float)*nq*topk));
  CKC(cudaMemcpy(qd,query,sizeof(float)*nq*dim,cudaMemcpyDefault));

  DLManagedTensor qt; qt.dl_tensor.data=qd; qt.dl_tensor.device.device_type=kDLCUDA;
  qt.dl_tensor.ndim=2; qt.dl_tensor.dtype.code=kDLFloat; qt.dl_tensor.dtype.bits=32; qt.dl_tensor.dtype.lanes=1;
  long qs[2]={nq,dim}; qt.dl_tensor.shape=qs; qt.dl_tensor.strides=0;
  DLManagedTensor nt; nt.dl_tensor.data=nd; nt.dl_tensor.device.device_type=kDLCUDA;
  nt.dl_tensor.ndim=2; nt.dl_tensor.dtype.code=kDLUInt; nt.dl_tensor.dtype.bits=32; nt.dl_tensor.dtype.lanes=1;
  long nsh[2]={nq,topk}; nt.dl_tensor.shape=nsh; nt.dl_tensor.strides=0;
  DLManagedTensor dt; dt.dl_tensor.data=dd; dt.dl_tensor.device.device_type=kDLCUDA;
  dt.dl_tensor.ndim=2; dt.dl_tensor.dtype.code=kDLFloat; dt.dl_tensor.dtype.bits=32; dt.dl_tensor.dtype.lanes=1;
  long dsh[2]={nq,topk}; dt.dl_tensor.shape=dsh; dt.dl_tensor.strides=0;

  cuvsCagraSearchParams_t sp; CK(cuvsCagraSearchParamsCreate(&sp));
  cuvsFilter f; f.type=NO_FILTER; f.addr=(uintptr_t)0;
  CK(cuvsCagraSearch(res,sp,idx,&qt,&nt,&dt,f));
  CKC(cudaMemcpy(out_ids,nd,sizeof(unsigned)*nq*topk,cudaMemcpyDefault));

  cuvsCagraSearchParamsDestroy(sp);
  cuvsRMMFree(res,dd,sizeof(float)*nq*topk); cuvsRMMFree(res,nd,sizeof(unsigned)*nq*topk); cuvsRMMFree(res,qd,sizeof(float)*nq*dim);
  cuvsCagraIndexDestroy(idx); cuvsCagraIndexParamsDestroy(ip); cuvsResourcesDestroy(res);
  return 0;
}
