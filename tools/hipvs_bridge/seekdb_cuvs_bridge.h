#ifndef SEEKDB_CUVS_BRIDGE_H
#define SEEKDB_CUVS_BRIDGE_H
#ifdef __cplusplus
extern "C" {
#endif
/* ABI-compatible with aadf910a and official-v5. L2, row-major float32 only.
 * Build: GPU CAGRA -> a full HNSW hierarchy. Search: CPU HNSW host tensors.
 * The historical one-shot symbol name is retained, but now uses this HNSW path.
 * Returned IDs remain uint32 row offsets; the caller maps them to database IDs.
 * A handle must not be freed concurrently with any other operation on it. */
void* seekdb_cuvs_build(const float* base, long n, long dim);
int seekdb_cuvs_search(void* handle, const float* query, long nq, long topk,
                       unsigned int* out_ids, float* out_dist);
void seekdb_cuvs_free(void* handle);
int seekdb_cuvs_cagra_knn(const float* base, long n, long dim,
                          const float* query, long nq, long topk,
                          unsigned int* out_ids);
/* v5 caller embeds this opaque single-file snapshot into its OBCUVS02 state.
 * It contains the HNSW graph/data, not CAGRA-only state or a sidecar pathname. */
int seekdb_cuvs_serialize(void* handle, const char* filename);
void* seekdb_cuvs_deserialize(const char* filename, long n, long dim);
#ifdef __cplusplus
}
#endif
#endif
