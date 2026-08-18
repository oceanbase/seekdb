/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define USING_LOG_PREFIX LIB

#include "ob_vsag_adaptor.h"
#include <map>
#include <mutex>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include "vsag/vsag.h"
#include "vsag/errors.h"
#include "vsag/dataset.h"
#include "vsag/search_param.h"
#include "vsag/index.h"
#include "vsag/options.h"
#include "vsag/factory.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/oblog/ob_log.h"
#include "lib/worker.h"

// [hipVS/cuVS] GPU vector-search bridge symbol, implemented in
// libseekdb_cuvs_bridge.so (internally uses cuVS CAGRA on the GPU).
extern "C" int   seekdb_cuvs_cagra_knn(const float *base, long n, long dim,
                                       const float *query, long nq, long topk,
                                       unsigned int *out_ids);
extern "C" void *seekdb_cuvs_build(const float *base, long n, long dim);
extern "C" int   seekdb_cuvs_search(void *handle, const float *query, long nq, long topk,
                                    unsigned int *out_ids, float *out_dist);
extern "C" void  seekdb_cuvs_free(void *handle);

namespace oceanbase {
namespace common {
namespace obvsag {

using namespace vsag;

// ==== [hipVS/cuVS] optional GPU CAGRA backend, gated by env OB_VSAG_USE_CUVS=1 ====
// When enabled, build_index also builds a cuVS CAGRA index (keyed by the index
// handle) and knn_search serves top-k from the AMD GPU via hipVS libcuvs_c. This
// runs seekdb's real vector-adaptor data path on the GPU. PoC: single writer per
// index, results allocated with the handle's allocator (drop-in with VSAG path).
namespace {
struct ObCuvsEntry {
  void *bridge_ = nullptr;      // opaque handle from seekdb_cuvs_build
  int dim_ = 0;
  std::vector<int64_t> ids_;    // CAGRA row offset -> external vid (of the built index)
  std::vector<float> buf_vecs_; // accumulated vectors (row-major) from add_index
  std::vector<int64_t> buf_ids_;// accumulated external vids from add_index
  size_t built_n_ = 0;          // #vectors present when cuVS was last (re)built
};
static std::mutex g_ob_cuvs_mu;
static std::map<void *, ObCuvsEntry *> g_ob_cuvs_reg;
static inline bool ob_cuvs_enabled() {
  const char *e = ::getenv("OB_VSAG_USE_CUVS");
  return e != nullptr && e[0] == '1';
}
static std::mutex g_ob_vsag_trace_mu;
static void ob_vsag_trace(const char *fn, const void *h, long a, long b) {
  const char *e = ::getenv("OB_VSAG_TRACE");
  if (e == nullptr || e[0] != '1') { return; }
  const char *path = ::getenv("OB_VSAG_TRACE_FILE");
  if (path == nullptr) { path = "/tmp/obvsag_trace.log"; }
  std::lock_guard<std::mutex> guard(g_ob_vsag_trace_mu);
  FILE *tf = ::fopen(path, "a");
  if (tf == nullptr) { return; }
  ::fprintf(tf, "%-14s handle=%p a=%ld b=%ld\n", fn, h, a, b);
  ::fclose(tf);
}
static void ob_cuvs_register(void *key, const float *vectors, const int64_t *ids,
                             int dim, int size) {
  void *bridge = seekdb_cuvs_build(vectors, size, dim);
  if (bridge == nullptr) { return; }
  ObCuvsEntry *ent = new ObCuvsEntry();
  ent->bridge_ = bridge; ent->dim_ = dim; ent->ids_.assign(ids, ids + size);
  std::lock_guard<std::mutex> guard(g_ob_cuvs_mu);
  ObCuvsEntry *&slot = g_ob_cuvs_reg[key];
  if (slot != nullptr) { seekdb_cuvs_free(slot->bridge_); delete slot; }
  slot = ent;
}
// Buffer vectors arriving via add_index. Plain HNSW builds BOTH its delta and its
// snapshot incrementally via add_index (one row at a time), so this is where the
// real data flows -- unlike build_index which is only used by the HNSW_SQ bulk path.
static void ob_cuvs_add(void *key, const float *vectors, const int64_t *ids,
                        int dim, int size) {
  if (vectors == nullptr || ids == nullptr || dim <= 0 || size <= 0) { return; }
  std::lock_guard<std::mutex> guard(g_ob_cuvs_mu);
  ObCuvsEntry *&slot = g_ob_cuvs_reg[key];
  if (slot == nullptr) { slot = new ObCuvsEntry(); slot->dim_ = dim; }
  if (slot->dim_ == 0) { slot->dim_ = dim; }
  if (slot->dim_ != dim) { return; }
  slot->buf_vecs_.insert(slot->buf_vecs_.end(), vectors,
                         vectors + static_cast<size_t>(dim) * size);
  slot->buf_ids_.insert(slot->buf_ids_.end(), ids, ids + size);
}
static void ob_cuvs_erase(void *key) {
  std::lock_guard<std::mutex> guard(g_ob_cuvs_mu);
  auto it = g_ob_cuvs_reg.find(key);
  if (it != g_ob_cuvs_reg.end()) {
    if (it->second->bridge_) { seekdb_cuvs_free(it->second->bridge_); }
    delete it->second; g_ob_cuvs_reg.erase(it);
  }
}
struct ObCuvsJob {
  ObCuvsEntry *ent; const float *query; int64_t topk; int dim; size_t n;
  bool need_build; std::vector<unsigned> *off; std::vector<float> *dst;
  int rc_; bool built_;
};
// Runs on a dedicated large-stack pthread: cuVS CAGRA build/search overflow the
// small (~1.5MB) OB worker-thread stack.
static void *ob_cuvs_job(void *arg) {
  ObCuvsJob *j = static_cast<ObCuvsJob *>(arg);
  if (j->need_build) {
    void *nb = seekdb_cuvs_build(j->ent->buf_vecs_.data(), static_cast<long>(j->n),
                                 static_cast<long>(j->dim));
    if (nb != nullptr) {
      if (j->ent->bridge_ != nullptr) { seekdb_cuvs_free(j->ent->bridge_); }
      j->ent->bridge_ = nb; j->ent->built_n_ = j->n; j->ent->ids_ = j->ent->buf_ids_;
      j->built_ = true;
    }
  }
  // Freshness/correctness: only serve from the GPU when the cuVS index is
  // up-to-date with the buffer. If rows were added since the last build, the
  // caller falls back to CPU VSAG (which is always fresh). This also gives the
  // desired split: streaming delta -> VSAG, stable snapshot -> cuVS.
  if (j->ent->bridge_ != nullptr && j->ent->built_n_ == j->n) {
    j->rc_ = seekdb_cuvs_search(j->ent->bridge_, j->query, 1, j->topk,
                                j->off->data(), j->dst->data());
  }
  return nullptr;
}

// [BATCH] Feed nq probe vectors to ONE cuVS call (nq>1). Same large-stack
// pthread pattern as the single-query path (cuVS overflows the OB worker stack).
struct ObCuvsBatchJob {
  ObCuvsEntry *ent; const float *queries; long nq; long topk; size_t n;
  bool need_build; unsigned *off; float *dst; long served_; bool built_;
};
static void *ob_cuvs_batch_job(void *arg) {
  ObCuvsBatchJob *j = static_cast<ObCuvsBatchJob *>(arg);
  if (j->need_build) {
    void *nb = seekdb_cuvs_build(j->ent->buf_vecs_.data(), static_cast<long>(j->n),
                                 static_cast<long>(j->ent->dim_));
    if (nb != nullptr) {
      if (j->ent->bridge_ != nullptr) { seekdb_cuvs_free(j->ent->bridge_); }
      j->ent->bridge_ = nb; j->ent->built_n_ = j->n; j->ent->ids_ = j->ent->buf_ids_;
      j->built_ = true;
    }
  }
  if (j->ent->bridge_ != nullptr && j->ent->built_n_ == j->n) {
    if (seekdb_cuvs_search(j->ent->bridge_, j->queries, j->nq, j->topk,
                           j->off, j->dst) == 0) { j->served_ = j->nq; }
  }
  return nullptr;
}

// [BATCH one-shot] Build a CAGRA over raw base + batch-search nq queries + free,
// all on a 32MB-stack pthread (cuVS overflows the small PL/worker stack). Used by
// dbms_vector.batch_knn, which reads vectors from SQL (no add_index registry).
struct ObCuvsRawBatchJob {
  const float *base; long n; long dim; const float *query; long nq; long topk;
  unsigned *off; float *dst; long served_;
};
static void *ob_cuvs_raw_batch_job(void *arg) {
  ObCuvsRawBatchJob *j = static_cast<ObCuvsRawBatchJob *>(arg);
  void *h = seekdb_cuvs_build(j->base, j->n, j->dim);
  if (h != nullptr) {
    if (seekdb_cuvs_search(h, j->query, j->nq, j->topk, j->off, j->dst) == 0) {
      j->served_ = j->nq;
    }
    seekdb_cuvs_free(h);
  }
  return nullptr;
}

static const size_t OB_CUVS_MIN_PTS = 256;  // CAGRA needs a graph; below this -> VSAG

static bool ob_cuvs_try_search(void *key, vsag::Allocator *alloc,
                               const float *query, int /*dim*/, int64_t topk,
                               const float *&dist, const int64_t *&ids,
                               int64_t &result_size, void *filter, bool reverse_filter) {
  if (topk <= 0) { return false; }
  std::lock_guard<std::mutex> guard(g_ob_cuvs_mu);
  auto it = g_ob_cuvs_reg.find(key);
  if (it == g_ob_cuvs_reg.end()) { return false; }
  ObCuvsEntry *ent = it->second;
  // Lazily (re)build the GPU CAGRA index when there are enough points and either
  // nothing is built yet or the buffer grew materially (>=2x). Build+search run on
  // a 32MB-stack pthread (cuVS overflows the OB worker stack otherwise).
  const size_t n = ent->buf_ids_.size();
  const bool need_build =
      (n >= OB_CUVS_MIN_PTS && (ent->bridge_ == nullptr || n >= ent->built_n_ * 2));
  std::vector<unsigned> off(topk);
  std::vector<float> dst(topk);
  ObCuvsJob job{ent, query, topk, static_cast<int>(ent->dim_), n, need_build, &off, &dst, -1, false};
  pthread_t tid; pthread_attr_t attr; pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 32UL * 1024 * 1024);
  if (pthread_create(&tid, &attr, ob_cuvs_job, &job) == 0) { pthread_join(tid, nullptr); }
  pthread_attr_destroy(&attr);
  if (job.built_) { ob_vsag_trace("cuvs_build", key, static_cast<long>(ent->dim_), static_cast<long>(n)); }
  if (ent->bridge_ == nullptr || job.rc_ != 0) { return false; }
  // Post-filter: cuVS returned UNFILTERED top-k. Apply the query's row filter
  // (WHERE predicate / deleted rows) with the SAME semantics VSAG uses: a row is
  // valid iff reverse_filter ? test(vid) : !test(vid). If ANY of the top-k is
  // excluded, fall back to VSAG (the true filtered top-k may rank beyond k here).
  if (filter != nullptr) {
    FilterInterface *fi = static_cast<FilterInterface *>(filter);
    for (int64_t i = 0; i < topk; ++i) {
      int64_t vid = (off[i] < ent->ids_.size()) ? static_cast<int64_t>(ent->ids_[off[i]]) : -1;
      const bool valid = reverse_filter ? fi->test(vid) : !fi->test(vid);
      if (!valid) { return false; }
    }
  }
  int64_t *out_ids = static_cast<int64_t *>(
      alloc ? alloc->Allocate(sizeof(int64_t) * topk) : ::malloc(sizeof(int64_t) * topk));
  float *out_dist = static_cast<float *>(
      alloc ? alloc->Allocate(sizeof(float) * topk) : ::malloc(sizeof(float) * topk));
  if (out_ids == nullptr || out_dist == nullptr) {
    if (alloc) { if (out_ids) alloc->Deallocate(out_ids); if (out_dist) alloc->Deallocate(out_dist); }
    else { ::free(out_ids); ::free(out_dist); }
    return false;
  }
  for (int64_t i = 0; i < topk; ++i) {
    unsigned o = off[i];
    out_ids[i] = (o < ent->ids_.size()) ? ent->ids_[o] : -1;
    out_dist[i] = dst[i];
  }
  ids = out_ids; dist = out_dist; result_size = topk;
  ob_vsag_trace("cuvs_serve", key, static_cast<long>(topk), static_cast<long>(ent->ids_.size()));
  return true;
}
}  // anonymous namespace

static int vsag_errcode2ob(vsag::ErrorType vsag_errcode)
{
  int ret = OB_ERR_VSAG_RETURN_ERROR;
  switch (vsag_errcode) {
    case vsag::ErrorType::INVALID_ARGUMENT: {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid vsag parameter", K(ret), K(vsag_errcode));
      break;
    }
    case vsag::ErrorType::UNSUPPORTED_INDEX:
    case vsag::ErrorType::UNSUPPORTED_INDEX_OPERATION: {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("not support vsag feature", K(ret), K(vsag_errcode));
      break;
    }
    case vsag::ErrorType::DIMENSION_NOT_EQUAL: {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("the dimension of request is NOT equal to index", K(ret), K(vsag_errcode));
      break;
    }
    case vsag::ErrorType::INDEX_EMPTY: {
      ret = OB_OP_NOT_ALLOW;
      LOG_WARN("index is empty, cannot search or serialize", K(ret), K(vsag_errcode));
      break;
    }
    case vsag::ErrorType::NO_ENOUGH_MEMORY: {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc memory in vasg", K(ret), K(vsag_errcode));
      break;
    }
    default: {
      ret = OB_ERR_VSAG_RETURN_ERROR;
      LOG_WARN("vsag return error", K(ret), K(vsag_errcode));
      break;
    }
  }
  return ret;
}

static void fill_vsag_error_message(const vsag::Error &error, std::string &err_msg)
{
  err_msg = error.message;
}

static void adjust_create_index_max_degree(const IndexType index_type, int &max_degree)
{
  // hgraph of vsag needs to be multiplied by 2 so as to align recall with hnsw
  if (HNSW_SQ_TYPE == index_type || HNSW_BQ_TYPE == index_type || HGRAPH_TYPE == index_type) {
    max_degree *= 2;
    LOG_INFO("change max_degree for hgraph", K(index_type), K(max_degree), K(lbt()));
  }
}

class ObVasgFilter final : public vsag::Filter {
public:
  ObVasgFilter(float valid_ratio,
               const std::function<bool(int64_t)> &vid_fallback_func,
               const std::function<bool(const char *)> &exinfo_fallback_func)
      : valid_ratio_(valid_ratio), vid_fallback_func_(vid_fallback_func),
        exinfo_fallback_func_(exinfo_fallback_func){};

  ~ObVasgFilter() {}

  bool CheckValid(int64_t id) const override { return !vid_fallback_func_(id); }

  bool CheckValid(const char *data) const override {
    return !exinfo_fallback_func_(data);
  }

  float ValidRatio() const override { return valid_ratio_; }

private:
  float valid_ratio_;
  std::function<bool(int64_t)> vid_fallback_func_{nullptr};
  std::function<bool(const char *)> exinfo_fallback_func_{nullptr};
};

class HnswIndexHandler {
public:
  HnswIndexHandler(bool is_create, bool is_build, bool use_static,
                   const char *dtype, const char *metric, int max_degree,
                   int ef_construction, int ef_search, int dim,
                   IndexType index_type, std::shared_ptr<vsag::Index> index,
                   vsag::Allocator *allocator, uint64_t extra_info_size,
                   int16_t refine_type, int16_t bq_bits_query, bool bq_use_fht)
      : is_created_(is_create), is_build_(is_build), use_static_(use_static),
        dtype_(dtype), metric_(metric), max_degree_(max_degree),
        ef_construction_(ef_construction), ef_search_(ef_search), dim_(dim),
        index_type_(index_type), index_(index), allocator_(allocator),
        extra_info_size_(extra_info_size), refine_type_(refine_type),
        bq_bits_query_(bq_bits_query), bq_use_fht_(bq_use_fht) {}

  HnswIndexHandler(bool is_create, bool is_build, bool use_static, const char *dtype, const char *metric,
      IndexType index_type, std::shared_ptr<vsag::Index> index, vsag::Allocator *allocator, uint64_t extra_info_size,
      bool use_reorder, float doc_prune_ratio, int window_size)
      : is_created_(is_create),
        is_build_(is_build),
        use_static_(use_static),
        dtype_(dtype),
        metric_(metric),
        index_type_(index_type),
        index_(index),
        allocator_(allocator),
        extra_info_size_(extra_info_size),
        use_reorder_(use_reorder),
        doc_prune_ratio_(doc_prune_ratio),
        window_size_(window_size)
  {}

  ~HnswIndexHandler() {
    index_ = nullptr;
    LOG_INFO("[OBVSAG] after deconstruction, hnsw index", KP(allocator_), K(index_.use_count()), K(lbt()));
  }
  int build_index(const vsag::DatasetPtr &base);
  int get_index_number();
  int add_index(const vsag::DatasetPtr &incremental);
  int cal_distance_by_id(const float *vector, const int64_t *ids, int64_t count,
                         const float *&dist);
  int cal_distance_by_id(uint32_t len, uint32_t *dims, float *vals,
                        const int64_t *ids, int64_t count, const float *&dist);
  int get_extra_info_by_ids(const int64_t *ids, int64_t count,
                            char *extra_infos);
  int get_vid_bound(int64_t &min_vid, int64_t &max_vid);
  uint64_t estimate_memory(const uint64_t row_count, const bool is_build);
  int knn_search(const vsag::DatasetPtr &query, int64_t topk,
                 const std::string &parameters, const float *&dist,
                 const int64_t *&ids, int64_t &result_size, float valid_ratio,
                 int index_type, FilterInterface *bitmap, bool reverse_filter,
                 bool need_extra_info, const char *&extra_infos,
                 void *allocator, float distance_threshold = FLT_MAX);
  int knn_search(const vsag::DatasetPtr &query, int64_t topk,
                 const std::string &parameters, const float *&dist,
                 const int64_t *&ids, int64_t &result_size, float valid_ratio,
                 int index_type, FilterInterface *bitmap, bool reverse_filter,
                 bool need_extra_info, const char *&extra_infos,
                 void *&iter_ctx, bool is_last_search, void *allocator);
  int immutable_optimize();

  std::shared_ptr<vsag::Index> &get_index() { return index_; }
  void set_index(std::shared_ptr<vsag::Index> hnsw) { index_ = hnsw; }
  vsag::Allocator *get_allocator() const { return allocator_; }
  inline bool get_use_static() const { return use_static_; }
  inline int get_max_degree() const { return max_degree_; }
  inline int get_ef_construction() const { return ef_construction_; }
  inline int get_index_type() const { return (int)index_type_; }
  const char *get_dtype() const { return dtype_; }
  const char *get_metric() const { return metric_; }
  inline int get_ef_search() const { return ef_search_; }
  inline int get_dim() const { return dim_; }
  inline uint64_t get_extra_info_size() const { return extra_info_size_; }
  inline int16_t get_refine_type() const { return refine_type_; }
  inline int16_t get_bq_bits_query() const { return bq_bits_query_; }
  inline bool get_bq_use_fht() const { return bq_use_fht_; };
  inline bool get_use_reorder() const { return use_reorder_; }
  inline float get_doc_prune_ratio() const { return doc_prune_ratio_; }
  inline int get_window_size() const { return window_size_; }

  TO_STRING_KV(KP(this), K_(is_created), K_(is_build), K_(use_static), KCSTRING_(dtype),
      KCSTRING_(metric), K_(max_degree), K_(ef_construction), K_(ef_search), K_(dim),
      K_(ef_search), K_(index_type), KP(index_.get()), KP_(allocator), K_(extra_info_size),
      K_(refine_type), K_(bq_bits_query), K_(bq_use_fht));

private:
  bool is_created_;
  bool is_build_;
  bool use_static_;
  const char *dtype_;
  const char *metric_;
  int max_degree_;
  int ef_construction_;
  int ef_search_;
  int dim_;
  IndexType index_type_;
  std::shared_ptr<vsag::Index> index_;
  vsag::Allocator *allocator_;
  uint64_t extra_info_size_;
  int16_t refine_type_;
  int16_t bq_bits_query_;
  bool bq_use_fht_;
  bool use_reorder_;
  float doc_prune_ratio_;
  int window_size_;
};

int HnswIndexHandler::build_index(const vsag::DatasetPtr &base)
{
  int ret = OB_SUCCESS;
  try {
    tl::expected<std::vector<int64_t>, Error> result = index_->Build(base);
    if (result.has_value()) {
    } else {
      ret = vsag_errcode2ob(result.error().type);
    }
  } catch (const std::exception &e) {
    ret = OB_ERR_VSAG_RETURN_ERROR;
    LOG_WARN("[OBVSAG] exception caught in build_index", "what", e.what());
  } catch (...) {
    ret = OB_ERR_VSAG_RETURN_ERROR;
    LOG_WARN("[OBVSAG] unknown exception caught in build_index");
  }
  return ret;
}

int HnswIndexHandler::get_index_number()
{
  return index_->GetNumElements();
}

int HnswIndexHandler::add_index(const vsag::DatasetPtr &incremental)
{
  int ret = OB_SUCCESS;
  tl::expected<std::vector<int64_t>, Error> result = index_->Add(incremental);
  if (result.has_value()) {
    LOG_DEBUG("add index success", K(get_index_number()));
  } else {
    ret = vsag_errcode2ob(result.error().type);
  }
  return ret;
}

int HnswIndexHandler::cal_distance_by_id(uint32_t len, uint32_t *dims, float *vals,
                                         const int64_t *ids, int64_t count,
                                         const float *&dist)
{
  int ret = OB_SUCCESS;
  vsag::SparseVector sparse;
  sparse.len_ = len;
  sparse.ids_ = dims;
  sparse.vals_ = vals;
  DatasetPtr query = vsag::Dataset::Make();
  query->NumElements(1)->SparseVectors(&sparse)->Owner(false);
  float *dist_tmp = (float*)allocator_->Allocate(count * sizeof(float));
  if (OB_ISNULL(dist_tmp)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc memory for cal_distance", K(ret), K(count));
  }
  // TODO(ningxin.ning): support CalcDistanceById in sparse vector
  for (int i = 0; i < count && OB_SUCC(ret); ++i) {
    // tl::expected<float, Error> result = index_->CalcDistanceById(query, ids[i]);
    // if (result.has_value()) {
    //   dist_tmp[i] = result.value();
    // } else {
    //   ret = vsag_errcode2ob(result.error().type);
    // }
    dist_tmp[i] = 0.1;
  }
  dist = dist_tmp;
  return ret;
}

int HnswIndexHandler::cal_distance_by_id(const float *vector,
                                         const int64_t *ids, int64_t count,
                                         const float *&dist)
{
  int ret = OB_SUCCESS;
  tl::expected<DatasetPtr, Error> result = index_->CalDistanceById(vector, ids, count);
  if (result.has_value()) {
    result.value()->Owner(false);
    dist = result.value()->GetDistances();
  } else {
    ret = vsag_errcode2ob(result.error().type);
  }
  return ret;
}

int HnswIndexHandler::get_extra_info_by_ids(const int64_t *ids, int64_t count,
                                            char *extra_infos)
{
  int ret = OB_SUCCESS;
  tl::expected<void, Error> result = index_->GetExtraInfoByIds(ids, count, extra_infos);
  if (result.has_value()) {
  } else {
    ret = vsag_errcode2ob(result.error().type);
  }
  return ret;
}

int HnswIndexHandler::get_vid_bound(int64_t &min_vid, int64_t &max_vid)
{
  int ret = OB_SUCCESS;
  int64_t element_cnt = index_->GetNumElements();
  if (element_cnt == 0) {
  } else {
    tl::expected<std::pair<int64_t, int64_t>, Error> result = index_->GetMinAndMaxId();
    if (result.has_value()) {
      min_vid = result.value().first;
      max_vid = result.value().second;
    } else {
      ret = vsag_errcode2ob(result.error().type);
    }
  }
  return ret;
}

uint64_t HnswIndexHandler::estimate_memory(const uint64_t row_count, const bool is_build)
{
  
  uint64_t size = 0;
  if (IPIVF_TYPE == index_type_) {
    // TODO(ningxin.ning): use vsag EstimateMemory
    size += 2 * sizeof(int64_t) * row_count;
    // nonzero dim = 100
    size += 100 * row_count * sizeof(float) * 2;
    if (use_reorder_) {
      size *= 2;
    }
  } else {
    size = index_->EstimateMemory(row_count);
  }
  if (HNSW_BQ_TYPE == index_type_ && is_build) {
    if (QuantizationType::SQ8 == refine_type_) {
      size += (row_count * dim_ * sizeof(uint8_t));
    } else {
      size += (row_count * dim_ * sizeof(float));
    }
  }
  return size;
}

int HnswIndexHandler::immutable_optimize()
{
  int ret = OB_SUCCESS;
  if (index_type_ == IPIVF_TYPE) {
    // TODO(ningxin.ning): support SetImmutable for sparse vector index
  } else {
    tl::expected<void, Error> res = index_->SetImmutable();
    if (res.has_value()) {
      LOG_INFO("[OBVSAG] set immutable success", KPC(this));
    } else {
      ret = vsag_errcode2ob(res.error().type);
      LOG_WARN("[OBVSAG] index set immutable error", K(ret), K(res.error().type));
    }
  }
  return ret;
}

int HnswIndexHandler::knn_search(const vsag::DatasetPtr &query, int64_t topk,
                                 const std::string &parameters,
                                 const float *&dist, const int64_t *&ids,
                                 int64_t &result_size, float valid_ratio,
                                 int index_type, FilterInterface *bitmap,
                                 bool reverse_filter, bool need_extra_info,
                                 const char *&extra_infos, void *allocator,
                                 float distance_threshold)
{
  int ret = OB_SUCCESS;
  std::function<bool(int64_t)> vid_filter = [bitmap, reverse_filter](int64_t id) -> bool {
    if (!reverse_filter) {
      return bitmap->test(id);
    } else {
      return !(bitmap->test(id));
    }
  };
  std::function<bool(const char *)> exinfo_filter = [bitmap, reverse_filter](const char *data) -> bool {
    if (!reverse_filter) {
      return bitmap->test(data);
    } else {
      return !(bitmap->test(data));
    }
  };

  std::shared_ptr<ObVasgFilter> vsag_filter = std::make_shared<ObVasgFilter>(valid_ratio, vid_filter, exinfo_filter);
  vsag::Allocator *vsag_allocator = nullptr;
  if (allocator != nullptr) vsag_allocator = static_cast<vsag::Allocator *>(allocator);
  tl::expected<std::shared_ptr<vsag::Dataset>, vsag::Error> result;
  if (index_type_ == IPIVF_TYPE) {
    result = index_->KnnSearch(query, topk, parameters, bitmap == nullptr ? nullptr : vsag_filter);
  } else {
    vsag::SearchParam search_param(false, parameters,
                                 bitmap == nullptr ? nullptr : vsag_filter,
                                 vsag_allocator);
    result = index_->KnnSearch(query, topk, search_param);
  }
  if (result.has_value()) {
    // the lifecycle of result
    result.value()->Owner(false);
    ids = result.value()->GetIds();
    dist = result.value()->GetDistances();
    result_size = result.value()->GetDim();
    if (need_extra_info) {
      extra_infos = result.value()->GetExtraInfos();
    }
  } else {
    ret = vsag_errcode2ob(result.error().type);
  }
  return ret;
}

int HnswIndexHandler::knn_search(const vsag::DatasetPtr &query, int64_t topk,
                                 const std::string &parameters,
                                 const float *&dist, const int64_t *&ids,
                                 int64_t &result_size, float valid_ratio,
                                 int index_type, FilterInterface *bitmap,
                                 bool reverse_filter, bool need_extra_info,
                                 const char *&extra_infos, void *&iter_ctx,
                                 bool is_last_search, void *allocator)
{
  int ret = OB_SUCCESS;
  std::function<bool(int64_t)> filter = [bitmap, reverse_filter](int64_t id) -> bool {
    if (!reverse_filter) {
      return bitmap->test(id);
    } else {
      return !(bitmap->test(id));
    }
  };
  std::function<bool(const char *)> exinfo_filter = [bitmap, reverse_filter](const char *data) -> bool {
    if (!reverse_filter) {
      return bitmap->test(data);
    } else {
      return !(bitmap->test(data));
    }
  };

  std::shared_ptr<ObVasgFilter> vsag_filter = std::make_shared<ObVasgFilter>(valid_ratio, filter, exinfo_filter);
  vsag::Allocator *vsag_allocator = nullptr;
  if (allocator != nullptr) vsag_allocator = static_cast<vsag::Allocator *>(allocator);
  vsag::IteratorContext *input_iter = static_cast<vsag::IteratorContext *>(iter_ctx);
  vsag::SearchParam search_param(true, parameters,
                                 bitmap == nullptr ? nullptr : vsag_filter,
                                 vsag_allocator, input_iter, is_last_search);
  tl::expected<std::shared_ptr<vsag::Dataset>, vsag::Error> result = index_->KnnSearch(query, topk, search_param);
  if (result.has_value()) {
    iter_ctx = search_param.iter_ctx;
    result.value()->Owner(false);
    ids = result.value()->GetIds();
    dist = result.value()->GetDistances();
    result_size = result.value()->GetDim();
    if (need_extra_info) {
      extra_infos = result.value()->GetExtraInfos();
    }
  } else {
    ret = vsag_errcode2ob(result.error().type);
  }
  return ret;
}

void set_log_level(int32_t ob_level_num)
{
  static std::map<int32_t, int32_t> ob2vsag_log_level = {
      {0 /*ERROR*/, vsag::Logger::Level::kERR},
      {1 /*WARN*/, vsag::Logger::Level::kWARN},
      {2 /*INFO*/, vsag::Logger::Level::kINFO},
      {3 /*EDIAG*/, vsag::Logger::Level::kERR},
      {4 /*WDIAG*/, vsag::Logger::Level::kWARN},
      {5 /*TRACE*/, vsag::Logger::Level::kTRACE},
      {6 /*DEBUG*/, vsag::Logger::Level::kDEBUG},
  };
  vsag::Options::Instance().logger()->SetLevel(
      static_cast<vsag::Logger::Level>(ob2vsag_log_level[ob_level_num]));
}

bool is_init_ = vsag::init();
bool is_init()
{
    LOG_INFO("[OBVSAG] Init VsagLib]:");
    if (is_init_) {
        LOG_INFO("[OBVSAG] Init VsagLib success");
    } else {
        LOG_INFO("[OBVSAG] Init VsagLib fail");
    }
    return is_init_; 
}

void set_logger(void *logger_ptr)
{
  vsag::Options::Instance().set_logger(static_cast<vsag::Logger *>(logger_ptr));
  vsag::Logger::Level log_level = static_cast<vsag::Logger::Level>(1); // default is debug level
  vsag::Options::Instance().logger()->SetLevel(log_level);
}

void set_block_size_limit(uint64_t size)
{
  vsag::Options::Instance().set_block_size_limit(size);
}

bool get_is_hgraph_type(uint8_t create_type) 
{
  bool res = false;
  switch (create_type) {
    case HNSW_TYPE: {
      res = false;
      break;
    }
    case HNSW_SQ_TYPE: 
    case HNSW_BQ_TYPE:
    case HGRAPH_TYPE: {
      res = true;
      break;
    }
  }
  return res;
}

const char* get_index_type_str(uint8_t create_type)
{
  const char* res;
  switch (create_type) {
    case HNSW_TYPE: {
      res = "hnsw";
      break;
    }
    case HNSW_SQ_TYPE: 
    case HNSW_BQ_TYPE:
    case HGRAPH_TYPE: {
      res = "hgraph";
      break;
    }
    case IPIVF_TYPE: {
      res = "sindi";
      break;
    }
  }
  return res;
}

const char* get_precise_quantization_type(const uint8_t type)
{
  const char* res = nullptr;
  if (type == QuantizationType::SQ8) {
    res = "sq8";
  } else {
    res = "fp32";
  }
  return res;
}


/**
  eg:
    hnsw: {
            "dtype": dtype, "metric_type": metric, "dim": dim, 
            "hnsw": {
              "max_degree": max_degree, "ef_construction": ef_construction, "ef_search": ef_search, "use_static": use_static
            }
          }
    hgraph: {
              "dtype": dtype, "metric_type": metric, "dim": dim, "extra_info_size": extra_info_size,
              "index_param": {
                "base_quantization_type": "fp32", "max_degree": max_degree, "ef_construction": ef_construction, "build_thread_count": 0
              }
            }
    sq: {
          "dtype": dtype, "metric_type": metric, "dim": dim, "extra_info_size": extra_info_size,
          "index_param": {
            "base_quantization_type": "sq8", "max_degree": max_degree, "ef_construction": ef_construction, "build_thread_count": 0
          }
        }
    bq: {
          "dtype": dtype, "metric_type": metric, "dim": dim, "extra_info_size": extra_info_size,
          "index_param": {
            "base_quantization_type": "rabitq", "max_degree": max_degree, "ef_construction": ef_construction, "build_thread_count": 0,
            "use_reorder": true, "ignore_reorder": true, "precise_quantization_type": "fp32", "precise_io_type": "block_memory_io"
          }
        }
*/
int construct_vsag_create_param(
    uint8_t create_type, const char *dtype, const char *metric, int dim,
    int max_degree, int ef_construction, int ef_search, void *allocator,
    int extra_info_size, int16_t refine_type, int16_t bq_bits_query,
    bool bq_use_fht, char *result_param_str)
{
  int ret = OB_SUCCESS;
  bool is_hgraph_type = get_is_hgraph_type(create_type);
  const char *index_type_str = is_hgraph_type ? "index_param" : "hnsw";
  const char *base_quantization_type;
  const int64_t buf_len = 1024;
  switch (create_type) {
  case HNSW_SQ_TYPE: {
    base_quantization_type = "sq8";
    break;
  }
  case HNSW_BQ_TYPE: {
    base_quantization_type = "rabitq";
    break;
  }
  case HGRAPH_TYPE: {
    base_quantization_type = "fp32";
    break;
  }
  default: {
    break;
  }
  }
  // ObIStreamBuf only supports seeking within the current callback buffer, while
  // VSAG's new format seeks to the footer. Keep the legacy format until global seek is supported.
  int64_t pos = 0;
  int64_t buff_size = 0;
  if (OB_FAIL(databuff_printf(result_param_str, buf_len, pos, "{\"dim\":%d",
                              int(dim)))) {
  } else if (OB_FAIL(databuff_printf(result_param_str, buf_len, pos,
                                     ",\"dtype\":\"%s\"",
                                     dtype))) {
  } else if (OB_FAIL(databuff_printf(result_param_str, buf_len, pos,
                                     ",\"metric_type\":\"%s\"",
                                     metric))) {
  } else if (extra_info_size > 0 &&
             OB_FAIL(databuff_printf(result_param_str, buf_len, pos,
                                 ",\"extra_info_size\": %d",
                                 extra_info_size))) {
    LOG_WARN("failed to fill result_param_str", K(ret), K(extra_info_size));
  } else if (OB_FAIL(databuff_printf(result_param_str, buf_len, pos,
                                 ",\"use_old_serial_format\":true"))) {
  } else if (OB_FAIL(databuff_printf(result_param_str, buf_len, pos,
                                     ",\"%s\":{",
                                     index_type_str))) {
  } else if (OB_FAIL(databuff_printf(
                 result_param_str, buf_len, pos, "\"ef_construction\":%d",
                 ef_construction))) {
  } else if (! is_hgraph_type && OB_FAIL(databuff_printf(result_param_str,
                                 buf_len, pos, ",\"ef_search\":%d",
                                 ef_search))) {
    LOG_WARN("failed to fill result_param_str", K(ret), K(ef_search));
  } else if (OB_FAIL(databuff_printf(result_param_str, buf_len, pos,
                                     ",\"max_degree\":%d",
                                     max_degree))) {
  } else if (is_hgraph_type &&
      OB_FAIL(databuff_printf(
          result_param_str, buf_len, pos,
          ",\"base_quantization_type\":\"%s\"",
          base_quantization_type))) {
    LOG_WARN("failed to fill result_param_str", K(ret), K(base_quantization_type));
  } else if (is_hgraph_type &&
             OB_FAIL(databuff_printf(result_param_str, buf_len, pos,
                                     ",\"build_thread_count\":%d",
                                     0))) {
    LOG_WARN("failed to fill result_param_str", K(ret));
  } else if (create_type == HNSW_BQ_TYPE &&
             OB_FAIL(databuff_printf(
                 result_param_str, buf_len, pos,
                 ",\"use_reorder\":true"))) {
    LOG_WARN("failed to fill result_param_str", K(ret));
  } else if (create_type == HNSW_BQ_TYPE &&
             OB_FAIL(databuff_printf(
                 result_param_str, buf_len, pos,
                 ",\"ignore_reorder\":true"))) {
    LOG_WARN("failed to fill result_param_str", K(ret));
  } else if (create_type == HNSW_BQ_TYPE &&
             OB_FAIL(databuff_printf(
                 result_param_str, buf_len, pos,
                 ",\"precise_quantization_type\":\"%s\"", get_precise_quantization_type(refine_type)))) {
    LOG_WARN("failed to fill result_param_str", K(ret), K(refine_type));
  } else if (create_type == HNSW_BQ_TYPE &&
             OB_FAIL(databuff_printf(result_param_str, buf_len, pos,
                                     ",\"precise_io_type\":\"block_memory_io\""))) {
    LOG_WARN("failed to fill result_param_str", K(ret));
  } else if (create_type == HNSW_BQ_TYPE &&
             OB_FAIL(databuff_printf(result_param_str, buf_len, pos,
                                     ",\"rabitq_bits_per_dim_query\":%d", bq_bits_query))) {
    LOG_WARN("failed to fill result_param_str", K(ret), K(bq_bits_query));
  } else if (create_type == HNSW_BQ_TYPE &&
             OB_FAIL(databuff_printf(result_param_str, buf_len, pos,
                                     ",\"rabitq_use_fht\":%s", (bq_use_fht ? "true" : "false")))) {
    LOG_WARN("failed to fill result_param_str", K(ret), K(bq_use_fht));
  } else if (OB_FAIL(databuff_printf(result_param_str, buf_len, pos,
                                     "}}"))) {
  }
  if (OB_SUCC(ret)) {
    LOG_INFO("build param", K(create_type), KCSTRING(result_param_str), K(lbt()));
  }
  return ret;
}

int construct_vsag_sindi_create_param(uint8_t create_type, const char *dtype, const char *metric, 
    void *allocator, int extra_info_size, bool use_reorder, float doc_prune_ratio, int window_size,
    char *result_param_str)
{
  int ret = OB_SUCCESS;
  const char *index_type_str = "index_param";
  const int64_t buf_len = 1024;

  int64_t pos = 0;
  int64_t buff_size = 0;
  // ObIStreamBuf exposes the serialized index through callback-backed chunks.
  // Skip seek-based footer handling and let SINDI read from that stream directly;
  // BufferStreamReader otherwise treats the current chunk length as the full stream.
  const bool deserialize_without_footer = true;
  const bool deserialize_without_buffer = true;
  if (OB_FAIL(databuff_printf(result_param_str, buf_len, pos, "{\"dtype\":\"%s\"", dtype))) {
  } else if (OB_FAIL(databuff_printf(result_param_str, buf_len, pos, ",\"metric_type\":\"%s\"", metric))) {
  } else if (OB_FAIL(databuff_printf(result_param_str, buf_len, pos, ",\"dim\": 1024"))) {
  } else if (extra_info_size > 0 &&
             OB_FAIL(databuff_printf(result_param_str, buf_len, pos, ",\"extra_info_size\": %d", extra_info_size))) {
  } else if (OB_FAIL(databuff_printf(result_param_str, buf_len, pos, ",\"%s\":{", index_type_str))) {
  } else if (OB_FAIL(databuff_printf(
                 result_param_str, buf_len, pos, "\"use_reorder\":%s", use_reorder ? "true" : "false"))) {
  } else if (OB_FAIL(databuff_printf(result_param_str, buf_len, pos, ",\"doc_prune_ratio\":%f", doc_prune_ratio))) {
  } else if (OB_FAIL(databuff_printf(result_param_str, buf_len, pos, ",\"window_size\":%d", window_size))) {
  } else if (OB_FAIL(databuff_printf(result_param_str, buf_len, pos,
                                 ",\"deserialize_without_footer\":%s",
                                 (deserialize_without_footer ? "true": "false")))) {
  } else if (OB_FAIL(databuff_printf(result_param_str, buf_len, pos,
                                 ",\"deserialize_without_buffer\":%s",
                                 (deserialize_without_buffer ? "true": "false")))) {
  } else if (OB_FAIL(databuff_printf(result_param_str, buf_len, pos, "}}"))) {
  }
  if (OB_SUCC(ret)) {
    LOG_INFO("build param", K(create_type), KCSTRING(result_param_str), K(lbt()));
  }
  return ret;
}

/**
  eg:
    hnsw : {"hnsw": {"ef_search": ef_search, "skip_ratio": 0.7}}
    hgraph : {"hgraph": {"ef_search": ef_search, "use_extra_info_filter": use_extra_info_filter}}
*/
int construct_vsag_search_param(uint8_t create_type, 
                                int64_t ef_search, 
                                bool use_extra_info_filter, 
                                char *result_param_str)
{
  int ret = OB_SUCCESS;
  bool is_hgraph_type = get_is_hgraph_type(create_type);
  const char *index_type_str = is_hgraph_type ? "hgraph" : "hnsw";
  int64_t pos = 0;
  int64_t buff_size = 0;
  int64_t buf_len = 1024;
  if (OB_FAIL(databuff_printf(result_param_str, 
                        buf_len, 
                        pos, 
                        "{\"%s\":{", index_type_str))) {
  } else if (OB_FAIL(databuff_printf(result_param_str, 
                        buf_len, 
                        pos, 
                        "\"ef_search\":%d", int(ef_search)))) {
  } else if (OB_FAIL(databuff_printf(result_param_str, 
                        buf_len, 
                        pos, 
                        ",\"skip_ratio\":%f", 0.7))) {
  } else if (is_hgraph_type && OB_FAIL(databuff_printf(result_param_str, 
                        buf_len, 
                        pos, 
                        ",\"use_extra_info_filter\":%s", use_extra_info_filter ? "true" : "false"))) {
    LOG_WARN("failed to fill result_param_str", K(ret), K(index_type_str));
  } else if (OB_FAIL(databuff_printf(result_param_str, 
                        buf_len, 
                        pos, 
                        "}}"))) {
  }
  if (OB_SUCC(ret)) {
    LOG_TRACE("search param", KCSTRING(result_param_str), K(lbt()));
  }
  return ret;
}

int construct_vsag_sindi_search_param(float query_prune_ratio, uint64_t n_candidate, 
                                char *result_param_str)
{
  int ret = OB_SUCCESS;
  const char *index_type_str = "sindi";
  int64_t pos = 0;
  int64_t buff_size = 0;
  int64_t buf_len = 1024;
  if (OB_FAIL(databuff_printf(result_param_str, 
                        buf_len, 
                        pos, 
                        "{\"%s\":{", index_type_str))) {
  } else if (OB_FAIL(databuff_printf(result_param_str, 
                        buf_len, 
                        pos, 
                        "\"query_prune_ratio\":%f", query_prune_ratio))) {
  } else if (OB_FAIL(databuff_printf(result_param_str, 
                        buf_len, 
                        pos, 
                        ",\"n_candidate\":%lu}}", n_candidate))) {
  }
  if (OB_SUCC(ret)) {
    LOG_TRACE("search param", KCSTRING(result_param_str), K(lbt()));
  }
  return ret;
}

int create_index(VectorIndexPtr &index_handler,
                 IndexType index_type, const char *dtype,
                 const char *metric, int dim, int max_degree,
                 int ef_construction, int ef_search, void *allocator,
                 int extra_info_size /* = 0*/, int16_t refine_type /*= 0*/,
                 int16_t bq_bits_query /*= 32*/, bool bq_use_fht /*= false*/)
{
  int ret = OB_SUCCESS;
  ob_vsag_trace("create_dense", (const void*)index_handler, (long)(dim), (long)((long)index_type));
  if (dtype == nullptr || metric == nullptr) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[OBVSAG] null pointer", KP(dtype), KP(metric));
  } else {
    vsag::Allocator *vsag_allocator = nullptr;
    if (allocator == nullptr) {
      vsag_allocator = nullptr;
      LOG_INFO("[OBVSAG] allocator is null , use default_allocator", K(index_type), K(lbt()));
    } else {
      vsag_allocator = static_cast<vsag::Allocator *>(allocator);
      LOG_INFO("[OBVSAG] use caller allocator ", K(index_type), K(lbt()));
    }
  
    adjust_create_index_max_degree(index_type, max_degree);

    const char* index_type_str = get_index_type_str(index_type);
    char result_param_str[1024] = {0};
    if (OB_FAIL(construct_vsag_create_param(
        uint8_t(index_type), dtype, metric, dim, max_degree, 
        ef_construction, ef_search, allocator, extra_info_size,
        refine_type, bq_bits_query, bq_use_fht, result_param_str))) {
    } else {
      const std::string input_json_str(result_param_str);
      tl::expected<std::shared_ptr<Index>, Error> index = vsag::Factory::CreateIndex(index_type_str, input_json_str, vsag_allocator);
      if (index.has_value()) {
        std::shared_ptr<vsag::Index> hnsw;
        hnsw = index.value();
        HnswIndexHandler *hnsw_index = new HnswIndexHandler(
            true, false, false, dtype, metric, max_degree, ef_construction,
            ef_search, dim, index_type, hnsw, vsag_allocator, extra_info_size,
            refine_type, bq_bits_query, bq_use_fht);
        if (OB_ISNULL(hnsw_index)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("new HnswIndexHandler fail", K(ret), K(index_type));
        } else {
          index_handler = static_cast<VectorIndexPtr>(hnsw_index);
        }
      } else {
        ret = vsag_errcode2ob(index.error().type);
        LOG_WARN("[OBVSAG] create index error happend",
            K(ret), KCSTRING(result_param_str), K(index.error().type), KCSTRING(index.error().message.c_str()));
      }
    }
  }
  return ret;
}

int validate_create_index(const CreateIndexParam &param, std::string &err_msg)
{
  int ret = OB_SUCCESS;
  err_msg.clear();
  if (param.dtype_ == nullptr || param.metric_ == nullptr) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[OBVSAG] null pointer", KP(param.dtype_), KP(param.metric_));
  } else {
    vsag::Allocator *vsag_allocator = nullptr;
    if (param.allocator_ == nullptr) {
      vsag_allocator = nullptr;
      LOG_INFO("[OBVSAG] allocator is null , use default_allocator", K(param.index_type_), K(lbt()));
    } else {
      vsag_allocator = static_cast<vsag::Allocator *>(param.allocator_);
      LOG_INFO("[OBVSAG] use caller allocator ", K(param.index_type_), K(lbt()));
    }

    const char *index_type_str = get_index_type_str(param.index_type_);
    char result_param_str[1024] = {0};
    if (param.is_sparse_) {
      if (OB_FAIL(construct_vsag_sindi_create_param(uint8_t(param.index_type_),
                                                    param.dtype_,
                                                    param.metric_,
                                                    param.allocator_,
                                                    param.extra_info_size_,
                                                    param.use_reorder_,
                                                    param.doc_prune_ratio_,
                                                    param.window_size_,
                                                    result_param_str))) {
      }
    } else {
      int max_degree = param.max_degree_;
      adjust_create_index_max_degree(param.index_type_, max_degree);
      if (OB_FAIL(construct_vsag_create_param(
          uint8_t(param.index_type_), param.dtype_, param.metric_, param.dim_, max_degree,
          param.ef_construction_, param.ef_search_, param.allocator_, param.extra_info_size_,
          param.refine_type_, param.bq_bits_query_, param.bq_use_fht_, result_param_str))) {
      }
    }
    if (OB_SUCC(ret)) {
      const std::string input_json_str(result_param_str);
      tl::expected<std::shared_ptr<Index>, Error> index =
          vsag::Factory::CreateIndex(index_type_str, input_json_str, vsag_allocator);
      if (!index.has_value()) {
        ret = vsag_errcode2ob(index.error().type);
        fill_vsag_error_message(index.error(), err_msg);
        LOG_WARN("[OBVSAG] validate create index error",
            K(ret), KCSTRING(result_param_str), K(index.error().type), KCSTRING(index.error().message.c_str()));
      }
    }
  }
  return ret;
}

int create_index(VectorIndexPtr &index_handler, IndexType index_type, const char *dtype, const char *metric,
    bool use_reorder, float doc_prune_ratio, int window_size, void *allocator, int extra_info_size /* = 0*/)
{
  int ret = OB_SUCCESS;
  ob_vsag_trace("create_reorder", (const void*)index_handler, (long)(0), (long)((long)index_type));
  if (dtype == nullptr || metric == nullptr) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[OBVSAG] null pointer", KP(dtype), KP(metric));
  } else {
    vsag::Allocator *vsag_allocator = nullptr;
    if (allocator == nullptr) {
      vsag_allocator = nullptr;
      LOG_INFO("[OBVSAG] allocator is null , use default_allocator", K(index_type), K(lbt()));
    } else {
      vsag_allocator = static_cast<vsag::Allocator *>(allocator);
      LOG_INFO("[OBVSAG] use caller allocator ", K(index_type), K(lbt()));
    }

    const char *index_type_str = get_index_type_str(index_type);
    char result_param_str[1024] = {0};
    if (OB_FAIL(construct_vsag_sindi_create_param(uint8_t(index_type),
            dtype,
            metric,
            allocator,
            extra_info_size,
            use_reorder,
            doc_prune_ratio,
            window_size,
            result_param_str))) {
    } else {
      const std::string input_json_str(result_param_str);
      tl::expected<std::shared_ptr<Index>, Error> index =
          vsag::Factory::CreateIndex(index_type_str, input_json_str, vsag_allocator);
      if (index.has_value()) {
        std::shared_ptr<vsag::Index> hnsw;
        hnsw = index.value();
        HnswIndexHandler *hnsw_index = new HnswIndexHandler(true,
            false,
            false,
            dtype,
            metric,
            index_type,
            hnsw,
            vsag_allocator,
            extra_info_size,
            use_reorder,
            doc_prune_ratio,
            window_size);
        if (OB_ISNULL(hnsw_index)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("new HnswIndexHandler fail", K(ret), K(index_type));
        } else {
          index_handler = static_cast<VectorIndexPtr>(hnsw_index);
        }
      } else {
        ret = vsag_errcode2ob(index.error().type);
        LOG_WARN("[OBVSAG] create index error happend", K(ret), KCSTRING(result_param_str), K(index.error().type));
      }
    }
  }
  return ret;
}

int build_index(VectorIndexPtr &index_handler, float *vector_list,
                int64_t *ids, int dim, int size, char *extra_infos /* = nullptr*/)
{
  int ret = OB_SUCCESS;
  ob_vsag_trace("build_dense", (const void*)index_handler, (long)(dim), (long)(size));
  if (index_handler == nullptr || vector_list == nullptr || ids == nullptr) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[OBVSAG] null pointer addr", KP(index_handler), KP(vector_list), K(ids));
  } else {
    HnswIndexHandler *hnsw = static_cast<HnswIndexHandler *>(index_handler);
    DatasetPtr dataset = vsag::Dataset::Make();
    dataset->Dim(dim)
        ->NumElements(size)
        ->Ids(ids)
        ->Float32Vectors(vector_list)
        ->Owner(false);
    if (extra_infos != nullptr) {
      dataset->ExtraInfos(extra_infos);
    }
    if (OB_FAIL(hnsw->build_index(dataset))) {
    } else if (ob_cuvs_enabled()) {
      ob_cuvs_register(index_handler, vector_list, ids, dim, size);
      LOG_INFO("[OBVSAG][cuVS] built GPU CAGRA index alongside VSAG",
               KP(index_handler), K(dim), K(size));
    }
  }
  return ret;
}

int build_index(VectorIndexPtr &index_handler, uint32_t *lens, uint32_t *dims, float *vals, int64_t *ids, int size,
    char *extra_info /* = nullptr*/)
{
  int ret = OB_SUCCESS;
  ob_vsag_trace("build_sparse", (const void*)index_handler, (long)(size), (long)(0));
  if (index_handler == nullptr || lens == nullptr || dims == nullptr || vals == nullptr || ids == nullptr) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[OBVSAG] null pointer addr", KP(index_handler), KP(lens), KP(dims), KP(vals), KP(vals), KP(ids));
  } else {
    uint32_t *cur_dims_ptr = dims;
    float *cur_vals_ptr = vals;
    std::vector<vsag::SparseVector> sparse_vectors(size);
    for (int i = 0; i < size; i++) {
      sparse_vectors[i].len_ = lens[i];
      sparse_vectors[i].ids_ = cur_dims_ptr;
      sparse_vectors[i].vals_ = cur_vals_ptr;
      cur_dims_ptr += lens[i];
      cur_vals_ptr += lens[i];
    }
    HnswIndexHandler *handler = static_cast<HnswIndexHandler *>(index_handler);
    DatasetPtr dataset = vsag::Dataset::Make();
    dataset->NumElements(size)->Ids(ids)->SparseVectors(sparse_vectors.data())->Owner(false);
    if (extra_info != nullptr) {
      dataset->ExtraInfos(extra_info);
    }
    if (OB_FAIL(handler->build_index(dataset))) {
    }
  }
  return ret;
}

int add_index(VectorIndexPtr &index_handler, float *vector,
              int64_t *ids, int dim, int size,
              char *extra_info /* = nullptr*/)
{
  int ret = OB_SUCCESS;
  ob_vsag_trace("add_dense", (const void*)index_handler, (long)(dim), (long)(size));
  if (ob_cuvs_enabled() && index_handler != nullptr &&
      strcmp(static_cast<HnswIndexHandler *>(index_handler)->get_metric(), "l2") == 0) {
    ob_cuvs_add(index_handler, vector, ids, dim, size);
  }
  if (index_handler == nullptr || vector == nullptr || ids == nullptr) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[OBVSAG] null pointer addr", KP(index_handler), KP(vector), KP(ids));
  } else {
    HnswIndexHandler *hnsw = static_cast<HnswIndexHandler *>(index_handler);
    // add index
    DatasetPtr incremental = vsag::Dataset::Make();
    incremental->Dim(dim)
        ->NumElements(size)
        ->Ids(ids)
        ->Float32Vectors(vector)
        ->Owner(false);
    if (extra_info != nullptr) {
      incremental->ExtraInfos(extra_info);
    }
    if (OB_FAIL(hnsw->add_index(incremental))) {
    }
  }
  return ret;
}

int add_index(VectorIndexPtr &index_handler, uint32_t *lens, uint32_t *dims, float *vals, int64_t *ids, int size,
    char *extra_info /* = nullptr*/)
{
  int ret = OB_SUCCESS;
  ob_vsag_trace("add_sparse", (const void*)index_handler, (long)(size), (long)(0));
  if (index_handler == nullptr || lens == nullptr || dims == nullptr || vals == nullptr || ids == nullptr) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[OBVSAG] null pointer addr", KP(index_handler), KP(lens), KP(dims), KP(vals), KP(vals), KP(ids));
  } else {
    uint32_t *cur_dims_ptr = dims;
    float *cur_vals_ptr = vals;
    std::vector<vsag::SparseVector> sparse_vectors(size);
    for (int i = 0; i < size; i++) {
      sparse_vectors[i].len_ = lens[i];
      sparse_vectors[i].ids_ = cur_dims_ptr;
      sparse_vectors[i].vals_ = cur_vals_ptr;
      cur_dims_ptr += lens[i];
      cur_vals_ptr += lens[i];
    }
    const uint32_t MAX_DIM_LIMIT = 500000;
    uint32_t max_dim = 0;
    for (int i = 0; i < size && OB_SUCC(ret); i++) {
      uint32_t length = sparse_vectors[i].len_;
      for (int j = 0; j < length && OB_SUCC(ret); j++) {
        max_dim = MAX(max_dim, sparse_vectors[i].ids_[j]);
        if (OB_UNLIKELY(max_dim > MAX_DIM_LIMIT)) {
          ret = OB_NOT_SUPPORTED;
          LOG_WARN("sparse vector dimension greater than 500000 is not supported.", K(ret), K(max_dim));
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "sparse vector dimension greater than 500000 is");
        }
      }
    }
    if (OB_FAIL(ret)) {
      // do nothing
    } else {
      HnswIndexHandler *hnsw = static_cast<HnswIndexHandler *>(index_handler);
      // add index
      DatasetPtr incremental = vsag::Dataset::Make();
      incremental->NumElements(size)->Ids(ids)->SparseVectors(sparse_vectors.data())->Owner(false);
      if (extra_info != nullptr) {
        incremental->ExtraInfos(extra_info);
      }
      if (OB_FAIL(hnsw->add_index(incremental))) {
      }
    }
  }
  return ret;
}

int get_index_type(VectorIndexPtr &index_handler)
{
  HnswIndexHandler *hnsw = static_cast<HnswIndexHandler *>(index_handler);
  return hnsw->get_index_type();
}

int get_index_number(VectorIndexPtr &index_handler, int64_t &size)
{
  int ret = OB_SUCCESS;
  if (index_handler == nullptr) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[OBVSAG] null pointer addr", K(index_handler));
  } else {
    HnswIndexHandler *hnsw = static_cast<HnswIndexHandler *>(index_handler);
    size = hnsw->get_index_number();
  }
  return ret;
}

int cal_distance_by_id(VectorIndexPtr &index_handler,
                       const float *vector, const int64_t *ids, int64_t count,
                       const float *&distances)
{
  int ret = OB_SUCCESS;
  if (index_handler == nullptr) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[OBVSAG] null pointer addr", K(index_handler));
  } else {
    HnswIndexHandler *hnsw = static_cast<HnswIndexHandler *>(index_handler);
    if (OB_FAIL(hnsw->cal_distance_by_id(vector, ids, count, distances))) {
    }
  }
  return ret;
}

int cal_distance_by_id(VectorIndexPtr &index_handler, uint32_t len, uint32_t *dims, float *vals, const int64_t *ids,
    int64_t count, const float *&distances)
{
  int ret = OB_SUCCESS;
  if (index_handler == nullptr) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[OBVSAG] null pointer addr", K(index_handler));
  } else {
    HnswIndexHandler *hnsw = static_cast<HnswIndexHandler *>(index_handler);
    if (OB_FAIL(hnsw->cal_distance_by_id(len, dims, vals, ids, count, distances))) {
    }
  }
  return ret;
}

int get_vid_bound(VectorIndexPtr &index_handler,
                         int64_t &min_vid, int64_t &max_vid)
{
  int ret = OB_SUCCESS;
  if (nullptr == index_handler) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[OBVSAG] null pointer addr", KP(index_handler));
  } else {
    HnswIndexHandler *hnsw = static_cast<HnswIndexHandler *>(index_handler);
    const IndexType index_type = static_cast<IndexType>(hnsw->get_index_type());
    if (index_type == IPIVF_TYPE) {
      // TODO(ningxin.ning): support get_vid_bound for ipivf
      min_vid = 0;
      max_vid = 0;
    } else {
      if (OB_FAIL(hnsw->get_vid_bound(min_vid, max_vid))) {
      }
    }
  }
  return ret;
}

int knn_search(VectorIndexPtr &index_handler, float *query_vector,
               int dim, int64_t topk, const float *&dist, const int64_t *&ids,
               int64_t &result_size, int ef_search, bool need_extra_info,
               const char *&extra_infos, void *invalid, bool reverse_filter,
               bool use_extra_info_filter, void *allocator, float valid_ratio, 
               float distance_threshold)
{
  int ret = OB_SUCCESS;
  ob_vsag_trace("knn_simple", (const void*)index_handler, (long)(dim), (long)(topk));
  if (index_handler == nullptr || query_vector == nullptr) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[OBVSAG] null pointer addr", KP(index_handler), KP(query_vector));
  } else {
    if (ob_cuvs_enabled()) {
      HnswIndexHandler *cuvs_hnsw = static_cast<HnswIndexHandler *>(index_handler);
      if (strcmp(cuvs_hnsw->get_metric(), "l2") == 0 &&
          ob_cuvs_try_search(index_handler, cuvs_hnsw->get_allocator(),
                             query_vector, dim, topk, dist, ids, result_size, invalid, reverse_filter)) {
        return OB_SUCCESS;
      }
    }
    FilterInterface *bitmap = static_cast<FilterInterface *>(invalid);
    HnswIndexHandler *hnsw = static_cast<HnswIndexHandler *>(index_handler);
    const IndexType index_type = static_cast<IndexType>(hnsw->get_index_type());
    char result_param_str[1024]= {0};
    const int64_t EF_SEARCH_LIMIT = 1000L;
    const int64_t AMPLIFICATION_FACTOR = 10;
    if (ef_search > EF_SEARCH_LIMIT) {
      int64_t index_number = hnsw->get_index_number();
      if (0 != index_number) {
        topk = topk < index_number ? topk : index_number;
      }
      int64_t ef_search_threshold = AMPLIFICATION_FACTOR * topk > EF_SEARCH_LIMIT ? AMPLIFICATION_FACTOR * topk : EF_SEARCH_LIMIT;
      ef_search = ef_search < ef_search_threshold ? ef_search : ef_search_threshold;
    }
    if (OB_FAIL(construct_vsag_search_param(uint8_t(index_type), ef_search, use_extra_info_filter, result_param_str))) {
    } else {
      const std::string input_json_string(result_param_str);
      DatasetPtr query = vsag::Dataset::Make();
      query->NumElements(1)->Dim(dim)->Float32Vectors(query_vector)->Owner(false);
      if (OB_FAIL(hnsw->knn_search(query, topk, input_json_string, dist, ids,
                                   result_size, valid_ratio, index_type, bitmap,
                                   reverse_filter, need_extra_info, extra_infos, allocator, distance_threshold))) {
      }
    }
  }
  return ret;
}

int knn_search(VectorIndexPtr &index_handler, float *query_vector,
               int dim, int64_t topk, const float *&dist, const int64_t *&ids,
               int64_t &result_size, int ef_search, bool need_extra_info,
               const char *&extra_infos, void *invalid, bool reverse_filter,
               bool use_extra_info_filter, float valid_ratio, void *&iter_ctx,
               bool is_last_search, void *allocator)
{
  int ret = OB_SUCCESS;
  ob_vsag_trace("knn_iterctx", (const void*)index_handler, (long)(dim), (long)(topk));
  if (index_handler == nullptr || query_vector == nullptr) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[OBVSAG] null pointer addr", K(index_handler), K(query_vector));
  } else {
    if (ob_cuvs_enabled()) {
      HnswIndexHandler *cuvs_hnsw = static_cast<HnswIndexHandler *>(index_handler);
      if (strcmp(cuvs_hnsw->get_metric(), "l2") == 0 &&
          ob_cuvs_try_search(index_handler, cuvs_hnsw->get_allocator(),
                             query_vector, dim, topk, dist, ids, result_size, invalid, reverse_filter)) {
        return OB_SUCCESS;
      }
    }
    FilterInterface *bitmap = static_cast<FilterInterface *>(invalid);
    HnswIndexHandler *hnsw = static_cast<HnswIndexHandler *>(index_handler);
    const IndexType index_type = static_cast<IndexType>(hnsw->get_index_type());
    char result_param_str[1024]= {0};
    const int64_t EF_SEARCH_LIMIT = 1000L;
    const int64_t AMPLIFICATION_FACTOR = 10;
    if (ef_search > EF_SEARCH_LIMIT) {
      int64_t index_number = hnsw->get_index_number();
      if (0 != index_number) {
        topk = topk < index_number ? topk : index_number;
      }
      int64_t ef_search_threshold = AMPLIFICATION_FACTOR * topk > EF_SEARCH_LIMIT ? AMPLIFICATION_FACTOR * topk : EF_SEARCH_LIMIT;
      ef_search = ef_search < ef_search_threshold ? ef_search : ef_search_threshold;
    }
    if (OB_FAIL(construct_vsag_search_param(uint8_t(index_type), ef_search, use_extra_info_filter, result_param_str))) {
    } else {
      const std::string input_json_string(result_param_str);
      DatasetPtr query = vsag::Dataset::Make();
      query->NumElements(1)->Dim(dim)->Float32Vectors(query_vector)->Owner(false);
      if (OB_FAIL(hnsw->knn_search(query, topk, input_json_string, dist, ids,
                            result_size, valid_ratio, index_type, bitmap,
                            reverse_filter, need_extra_info, extra_infos, iter_ctx,
                            is_last_search, allocator))) {
      }
    }
  }
  return ret;
}

int knn_search(obvsag::VectorIndexPtr &index_handler, uint32_t len, uint32_t *dims, float *vals, int64_t topk,
    const float *&result_dist, const int64_t *&result_ids, const char *&extra_infos, int64_t &result_size,
    float query_prune_ratio, int64_t n_candidate, void *invalid, bool reverse_filter,
    bool is_extra_info_filter, float valid_ratio, void *allocator, bool need_extra_info)
{
  int ret = OB_SUCCESS;
  ob_vsag_trace("knn_sparse", (const void*)index_handler, (long)(len), (long)(topk));
  if (index_handler == nullptr || dims == nullptr || vals == nullptr) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[OBVSAG] null pointer addr", K(index_handler), K(dims), K(vals));
  } else {
    FilterInterface *bitmap = static_cast<FilterInterface *>(invalid);
    HnswIndexHandler *hnsw = static_cast<HnswIndexHandler *>(index_handler);
    const IndexType index_type = static_cast<IndexType>(hnsw->get_index_type());
    char result_param_str[1024]= {0};
    if (OB_FAIL(construct_vsag_sindi_search_param(query_prune_ratio, n_candidate, result_param_str))) {
    } else if (len == 0) {
      result_size = 0;
    } else {
      const std::string input_json_string(result_param_str);
      vsag::SparseVector sparse;
      sparse.len_ = len;
      sparse.ids_ = dims;
      sparse.vals_ = vals;
      DatasetPtr query = vsag::Dataset::Make();
      query->NumElements(1)->SparseVectors(&sparse)->Owner(false);
      if (OB_FAIL(hnsw->knn_search(query, topk, input_json_string, result_dist, result_ids,
                            result_size, valid_ratio, index_type, bitmap,
                            reverse_filter, need_extra_info, extra_infos, allocator))) {
      }
    }
  }
  return ret;
}

int fserialize(VectorIndexPtr &index_handler, std::ostream &out_stream)
{
  int ret = OB_SUCCESS;
  if (index_handler == nullptr) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[OBVSAG] null pointer addr", K(index_handler));
  } else {
    HnswIndexHandler *hnsw = static_cast<HnswIndexHandler *>(index_handler);
    tl::expected<void, Error> bs = hnsw->get_index()->Serialize(out_stream);
    if (bs.has_value()) {
      LOG_INFO("[OBVSAG] serialize index success");
    } else {
      ret = vsag_errcode2ob(bs.error().type);
      LOG_WARN("[OBVSAG] fserialize error happend", K(ret), K(bs.error().type));
    }
  }
  return ret;
}

int fdeserialize(VectorIndexPtr &index_handler,
                 std::istream &in_stream)
{
  int ret = OB_SUCCESS;
  ob_vsag_trace("fdeserialize", (const void*)index_handler, (long)(0), (long)(0));
  if (index_handler == nullptr) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[OBVSAG] null pointer addr", K(index_handler));
  } else {
    HnswIndexHandler *hnsw = static_cast<HnswIndexHandler *>(index_handler);
    std::shared_ptr<vsag::Index> hnsw_index;
    bool use_static = hnsw->get_use_static();
    const char *metric = hnsw->get_metric();
    const char *dtype = hnsw->get_dtype();
    int max_degree = hnsw->get_max_degree();
    int ef_construction = hnsw->get_ef_construction();
    int ef_search = hnsw->get_ef_search();
    int dim = hnsw->get_dim();
    int index_type = hnsw->get_index_type();
    uint64_t extra_info_size = hnsw->get_extra_info_size();
    const char* index_type_str = get_index_type_str(index_type);
    int16_t refine_type = hnsw->get_refine_type();
    int16_t bq_bits_query = hnsw->get_bq_bits_query();
    bool bq_use_fht = hnsw->get_bq_use_fht();
    bool use_reorder = hnsw->get_use_reorder();
    float doc_prune_ratio = hnsw->get_doc_prune_ratio();
    int window_size = hnsw->get_window_size();

    char result_param_str[1024] = {0};
    if ((IndexType)index_type == IndexType::IPIVF_TYPE) {
      if (OB_FAIL(construct_vsag_sindi_create_param(uint8_t(index_type),
              dtype,
              metric,
              hnsw->get_allocator(),
              extra_info_size,
              use_reorder,
              doc_prune_ratio,
              window_size,
              result_param_str))) {
      }
    } else {
      if (OB_FAIL(construct_vsag_create_param(
        uint8_t(index_type), dtype, metric, dim, max_degree,
        ef_construction, ef_search, hnsw->get_allocator(),
        extra_info_size, refine_type, bq_bits_query, bq_use_fht, result_param_str))) {
      } 
    }
    if (OB_FAIL(ret)) {
    } else {
      const std::string input_json_str(result_param_str);
      tl::expected<std::shared_ptr<Index>, Error> index = vsag::Factory::CreateIndex(index_type_str, input_json_str, hnsw->get_allocator());
      if (index.has_value()) {
        hnsw_index = index.value();
        tl::expected<void, Error> bs = hnsw_index->Deserialize(in_stream);
        if (bs.has_value()) {
          hnsw->set_index(hnsw_index);
          LOG_INFO("[OBVSAG] fdeserialize success", KCSTRING(result_param_str));
        } else {
          ret = vsag_errcode2ob(bs.error().type);
          LOG_WARN("[OBVSAG] fdeserialize error", K(ret), K(bs.error().type));
        }
      } else {
        ret = vsag_errcode2ob(index.error().type);
        LOG_WARN("[OBVSAG] create index error", K(ret), K(index.error().type));
      }
    }
  }
  return ret;
}

int delete_index(VectorIndexPtr &index_handler)
{
  int ret = OB_SUCCESS;
  ob_vsag_trace("delete_index", (const void*)index_handler, (long)(0), (long)(0));
  LOG_INFO("[OBVSAG] delete index ",
      KP((void *)static_cast<HnswIndexHandler *>(index_handler)->get_index().get()),
      K(static_cast<HnswIndexHandler *>(index_handler)->get_index().use_count()), K(lbt()));
  if (index_handler != nullptr) {
    ob_cuvs_erase(index_handler);
    delete static_cast<HnswIndexHandler *>(index_handler);
    index_handler = nullptr;
  }
  return ret;
}

void delete_iter_ctx(void *iter_ctx)
{
  LOG_TRACE("[OBVAG] delete_iter_ctx", KP(iter_ctx), K(lbt()));
  if (iter_ctx != nullptr) {
    delete static_cast<vsag::IteratorContext *>(iter_ctx);
    iter_ctx = nullptr;
  }
}

int get_extra_info_by_ids(VectorIndexPtr &index_handler,
                          const int64_t *ids, int64_t count,
                          char *extra_infos)
{
  int ret = OB_SUCCESS;
  if (index_handler == nullptr) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[OBVSAG] null pointer addr", K(index_handler));
  } else {
    HnswIndexHandler *hnsw = static_cast<HnswIndexHandler *>(index_handler);
    if (OB_FAIL(hnsw->get_extra_info_by_ids(ids, count, extra_infos))) {
    }
  }
  return ret;
}

uint64_t estimate_memory(VectorIndexPtr &index_handler, const uint64_t row_count, const bool is_build) 
{
  uint64_t estimate_memory_size = 0;
  if (index_handler != nullptr) {
    HnswIndexHandler *hnsw = static_cast<HnswIndexHandler *>(index_handler);
    estimate_memory_size = hnsw->estimate_memory(row_count, is_build);
  }
  return estimate_memory_size;
}

int immutable_optimize(VectorIndexPtr& index_handler)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(index_handler)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[OBVSAG] null pointer addr", K(ret), KP(index_handler));
  } else {
    HnswIndexHandler *hnsw = static_cast<HnswIndexHandler *>(index_handler);
    if (OB_FAIL(hnsw->immutable_optimize())) {
    }
  }
  return ret;
}

int cuvs_cagra_knn(const float *base, long n, long dim,
                   const float *query, long nq, long topk, unsigned int *out_ids)
{
  // Route to the hipVS/cuVS GPU backend (bridge .so). Returns 0 on success.
  return ::seekdb_cuvs_cagra_knn(base, n, dim, query, nq, topk, out_ids);
}

// [hipVS/cuVS BATCH operator] Feed nq probe vectors (row-major, dim from the
// registered index) to ONE GPU call over an add_index-buffered index. Caller
// allocates out_ids[nq*topk] (original vids) and out_dist[nq*topk]. Returns the
// number of queries served (nq) or 0 to fall back to CPU. This is the seam a
// batched vector operator (similarity JOIN / bulk ANN) would call to exploit the
// ~50-260x GPU batch speedup over per-probe knn_search (single-query gets no win).
long cuvs_knn_search_batch(void *key, const float *queries, long nq, long topk,
                           int64_t *out_ids, float *out_dist)
{
  if (key == nullptr || queries == nullptr || out_ids == nullptr ||
      out_dist == nullptr || nq <= 0 || topk <= 0) { return 0; }
  std::lock_guard<std::mutex> guard(g_ob_cuvs_mu);
  auto it = g_ob_cuvs_reg.find(key);
  if (it == g_ob_cuvs_reg.end()) { return 0; }
  ObCuvsEntry *ent = it->second;
  const size_t n = ent->buf_ids_.size();
  const bool need_build =
      (n >= OB_CUVS_MIN_PTS && (ent->bridge_ == nullptr || n >= ent->built_n_ * 2));
  std::vector<unsigned> off(static_cast<size_t>(nq) * topk);
  std::vector<float> dst(static_cast<size_t>(nq) * topk);
  ObCuvsBatchJob job{ent, queries, nq, topk, n, need_build,
                     off.data(), dst.data(), 0, false};
  pthread_t tid; pthread_attr_t attr; pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 32UL * 1024 * 1024);
  if (pthread_create(&tid, &attr, ob_cuvs_batch_job, &job) == 0) { pthread_join(tid, nullptr); }
  pthread_attr_destroy(&attr);
  if (job.built_) { ob_vsag_trace("cuvs_batch_build", key, static_cast<long>(ent->dim_), static_cast<long>(n)); }
  if (job.served_ != nq) { return 0; }
  for (long q = 0; q < nq; ++q) {
    for (long i = 0; i < topk; ++i) {
      const size_t p = static_cast<size_t>(q) * topk + i;
      const unsigned o = off[p];
      out_ids[p] = (o < ent->ids_.size()) ? ent->ids_[o] : -1;
      out_dist[p] = dst[p];
    }
  }
  ob_vsag_trace("cuvs_batch", key, nq, topk);
  return nq;
}

// [hipVS/cuVS] One-shot RAW batch ANN for dbms_vector.batch_knn: build CAGRA over
// base (n x dim, row-major f32), batch-search nq queries in ONE GPU call, free.
// out_ids/out_dist are caller-allocated [nq*topk]; out_ids get cuVS ROW OFFSETS
// (0..n-1). Runs on a 32MB pthread. Returns nq on success, 0 on failure/disabled.
long cuvs_batch_knn(const float *base, long n, long dim,
                    const float *query, long nq, long topk,
                    unsigned int *out_ids, float *out_dist)
{
  if (!ob_cuvs_enabled()) { return 0; }
  if (base == nullptr || query == nullptr || out_ids == nullptr ||
      out_dist == nullptr || n <= 0 || dim <= 0 || nq <= 0 || topk <= 0) { return 0; }
  ObCuvsRawBatchJob job{base, n, dim, query, nq, topk, out_ids, out_dist, 0};
  pthread_t tid; pthread_attr_t attr; pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 32UL * 1024 * 1024);
  if (pthread_create(&tid, &attr, ob_cuvs_raw_batch_job, &job) == 0) { pthread_join(tid, nullptr); }
  pthread_attr_destroy(&attr);
  ob_vsag_trace("cuvs_raw_batch", base, nq, topk);
  return job.served_;
}

} // namespace obvsag
} // namespace common
} // namespace oceanbase
