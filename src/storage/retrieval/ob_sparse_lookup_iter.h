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

#ifndef OB_SPARSE_LOOKUP_ITER_H_
#define OB_SPARSE_LOOKUP_ITER_H_

#include "ob_i_sparse_retrieval_iter.h"

namespace oceanbase
{
namespace storage
{

// decorator class supporting functional lookup
class ObSRLookupIter : public ObISparseRetrievalMergeIter
{
public:
  ObSRLookupIter();
  virtual ~ObSRLookupIter() {}
  virtual int get_next_row() override;
  virtual int get_next_rows(const int64_t capacity, int64_t &count) override;
  int init(
      ObSparseRetrievalMergeParam &iter_param,
      ObISparseRetrievalMergeIter &merge_iter,
      ObIAllocator &iter_allocator,
      const int64_t cache_capacity);
  virtual void reuse() override;
  virtual void reset() override;
  INHERIT_TO_STRING_KV("ObISparseRetrievalMergeIter", ObISparseRetrievalMergeIter,
      K_(cache_capacity), K_(rangekey_size));
public:
  virtual int set_hints(
      const common::ObIArray<std::pair<ObDocIdExt, int>> &virtual_rangekeys,
      const int64_t size) = 0;
protected:
  virtual int inner_init() = 0;
  virtual int load_results() = 0;
  virtual int project_results(const int64_t capacity, int64_t &count) = 0;
protected:
  ObIAllocator *iter_allocator_;
  ObSparseRetrievalMergeParam *iter_param_;
  ObISparseRetrievalMergeIter *merge_iter_;
  int64_t cache_capacity_;
  int64_t rangekey_size_;
  ObFixedArray<ObDocIdExt, ObIAllocator> cached_domain_ids_; // can be sorted or unsorted by id
  common::ObDatumCmpFuncType cmp_func_;
  void (*set_datum_func_)(ObDatum &, const ObDocIdExt &);
private:
  DISALLOW_COPY_AND_ASSIGN(ObSRLookupIter);
};

// lookup iter for sorted results
class ObSRSortedLookupIter final : public ObSRLookupIter
{
public:
  ObSRSortedLookupIter();
  virtual ~ObSRSortedLookupIter() {}
  TO_STRING_EMPTY();
public:
  void reset() override;
  int set_hints(
      const common::ObIArray<std::pair<ObDocIdExt, int>> &virtual_rangekeys,
      const int64_t size) override;
protected:
  int inner_init() override;
  int load_results() override;
  int project_results(const int64_t capacity, int64_t &count) override;
protected:
  ObFixedArray<int64_t, ObIAllocator> reverse_hints_;
  ObFixedArray<double, ObIAllocator> cached_relevances_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObSRSortedLookupIter);
};

// lookup iter for unsorted results
class ObSRHashLookupIter final : public ObSRLookupIter
{
public:
  ObSRHashLookupIter();
  virtual ~ObSRHashLookupIter() {}
  TO_STRING_EMPTY();
public:
  void reset() override;
  int set_hints(
      const common::ObIArray<std::pair<ObDocIdExt, int>> &virtual_rangekeys,
      const int64_t size) override;
protected:
  int inner_init() override;
  int load_results() override;
  int project_results(const int64_t capacity, int64_t &count) override;
protected:
  typedef hash::ObHashMap<ObDocIdExt, double> ObSRTaaTHashMap;
  ObSRTaaTHashMap hash_map_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObSRHashLookupIter);
};

} // namespace storage
} // namespace oceanbase

#endif
