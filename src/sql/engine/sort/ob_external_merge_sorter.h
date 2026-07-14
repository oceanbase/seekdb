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

#ifndef OCEANBASE_SQL_ENGINE_SORT_OB_EXTERNAL_MERGE_SORTER_H_
#define OCEANBASE_SQL_ENGINE_SORT_OB_EXTERNAL_MERGE_SORTER_H_

#include "lib/container/ob_heap.h"
#include "sql/engine/sort/ob_sort_vec_op_chunk.h"

namespace oceanbase
{
namespace sql
{

template <typename Compare, typename Store_Row, bool has_addon>
class ObExternalMergeSorter
{
public:
  static const int64_t MAX_MERGE_WAYS = 256;
  typedef ObSortVecOpChunk<Store_Row, has_addon> ChunkType;
  typedef common::ObBinaryHeap<ChunkType *, Compare, MAX_MERGE_WAYS> MergeHeap;

  ObExternalMergeSorter(common::ObIAllocator &allocator, Compare &comp)
    : allocator_(allocator),
      comp_(comp),
      heap_(nullptr),
      heap_iter_begin_(false),
      is_inited_(false)
  {}

  ~ObExternalMergeSorter()
  {
    destroy();
  }

  void destroy()
  {
    if (nullptr != heap_) {
      heap_->~MergeHeap();
      allocator_.free(heap_);
      heap_ = nullptr;
    }
    heap_iter_begin_ = false;
    is_inited_ = false;
  }

  void reset()
  {
    if (nullptr != heap_) {
      heap_->reset();
    }
    heap_iter_begin_ = false;
    is_inited_ = false;
  }

  int init(common::ObDList<ChunkType> &chunks, const int64_t merge_ways)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(is_inited_)) {
      ret = OB_INIT_TWICE;
      SQL_ENG_LOG(WARN, "external merge sorter init twice", K(ret));
    } else if (OB_UNLIKELY(chunks.get_size() < 2 || merge_ways < 2
                           || merge_ways > chunks.get_size())) {
      ret = OB_INVALID_ARGUMENT;
      SQL_ENG_LOG(WARN, "invalid external merge argument",
                  K(ret), K(chunks.get_size()), K(merge_ways));
    } else if (nullptr == heap_
               && OB_ISNULL(heap_ = OB_NEWx(MergeHeap, (&allocator_), comp_, &allocator_))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SQL_ENG_LOG(WARN, "allocate external merge heap failed", K(ret));
    } else {
      heap_->reset();
      ChunkType *chunk = chunks.get_first();
      for (int64_t i = 0; OB_SUCC(ret) && i < merge_ways; ++i) {
        chunk->reset_row_iter();
        if (OB_FAIL(chunk->init_row_iter())) {
          SQL_ENG_LOG(WARN, "init chunk iterator failed", K(ret));
        } else if (OB_FAIL(chunk->get_next_row()) || nullptr == chunk->sk_row_) {
          if (OB_ITER_END == ret || OB_SUCCESS == ret) {
            ret = OB_ERR_UNEXPECTED;
            SQL_ENG_LOG(WARN, "non-empty chunk returned no row", K(ret), KP(chunk->sk_row_));
          } else {
            SQL_ENG_LOG(WARN, "get first chunk row failed", K(ret));
          }
        } else if (OB_FAIL(heap_->push(chunk))) {
          SQL_ENG_LOG(WARN, "push external merge chunk failed", K(ret));
        } else {
          chunk = chunk->get_next();
        }
      }
      if (OB_SUCC(ret)) {
        heap_iter_begin_ = false;
        is_inited_ = true;
      }
    }
    return ret;
  }

  int get_next_row(const Store_Row *&sk_row, const Store_Row *&addon_row)
  {
    int ret = OB_SUCCESS;
    ChunkType *chunk = nullptr;
    sk_row = nullptr;
    addon_row = nullptr;
    if (OB_FAIL(heap_next(chunk))) {
      if (OB_ITER_END != ret) {
        SQL_ENG_LOG(WARN, "external merge heap next failed", K(ret));
      }
    } else if (OB_ISNULL(chunk) || OB_ISNULL(chunk->sk_row_)) {
      ret = OB_ERR_UNEXPECTED;
      SQL_ENG_LOG(WARN, "invalid external merge chunk", K(ret), KP(chunk));
    } else {
      sk_row = chunk->sk_row_;
      if (has_addon) {
        addon_row = chunk->addon_row_;
      }
    }
    return ret;
  }

private:
  int heap_next(ChunkType *&chunk)
  {
    int ret = OB_SUCCESS;
    chunk = nullptr;
    if (OB_UNLIKELY(!is_inited_ || OB_ISNULL(heap_))) {
      ret = OB_NOT_INIT;
      SQL_ENG_LOG(WARN, "external merge sorter not init", K(ret), K(is_inited_), KP(heap_));
    } else {
      if (heap_iter_begin_) {
        if (!heap_->empty()) {
          ChunkType *top = heap_->top();
          bool is_end = false;
          if (OB_FAIL(top->get_next_row())) {
            if (OB_ITER_END == ret) {
              ret = OB_SUCCESS;
              is_end = true;
            } else {
              SQL_ENG_LOG(WARN, "get next chunk row failed", K(ret));
            }
          }
          if (OB_SUCC(ret)) {
            if (is_end) {
              if (OB_FAIL(heap_->pop())) {
                SQL_ENG_LOG(WARN, "external merge heap pop failed", K(ret));
              }
            } else if (OB_FAIL(heap_->replace_top(top))) {
              SQL_ENG_LOG(WARN, "external merge heap replace failed", K(ret));
            }
          }
        }
      } else {
        heap_iter_begin_ = true;
      }
      if (OB_SUCC(ret)) {
        if (heap_->empty()) {
          ret = OB_ITER_END;
        } else {
          chunk = heap_->top();
        }
      }
    }
    return ret;
  }

private:
  common::ObIAllocator &allocator_;
  Compare &comp_;
  MergeHeap *heap_;
  bool heap_iter_begin_;
  bool is_inited_;
};

} // namespace sql
} // namespace oceanbase

#endif /* OCEANBASE_SQL_ENGINE_SORT_OB_EXTERNAL_MERGE_SORTER_H_ */
