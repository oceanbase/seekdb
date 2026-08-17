#ifndef SEEKDB_CUVS_BRIDGE_H
#define SEEKDB_CUVS_BRIDGE_H
#ifdef __cplusplus
extern "C" {
#endif
/* Build a cuVS CAGRA index over base (n x dim, row-major float32) on the GPU and
 * search queries (nq x dim), writing top-k neighbor ids (uint32) to out_ids
 * (length nq*topk). Returns 0 on success, non-zero on error.
 * This is the ONLY symbol seekdb needs; all ROCm/cuVS deps live inside the .so. */
int seekdb_cuvs_cagra_knn(const float* base, long n, long dim,
                          const float* query, long nq, long topk,
                          unsigned int* out_ids);
#ifdef __cplusplus
}
#endif
#endif
