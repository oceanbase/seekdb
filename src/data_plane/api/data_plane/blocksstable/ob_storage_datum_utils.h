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

#ifndef OCEANBASE_DATA_PLANE_BLOCKSSTABLE_OB_STORAGE_DATUM_UTILS_H_
#define OCEANBASE_DATA_PLANE_BLOCKSSTABLE_OB_STORAGE_DATUM_UTILS_H_

#include "data_plane/blocksstable/ob_storage_datum.h"
#include "data_plane/meta/ob_fixed_meta_obj_array.h"
#include "share/datum/ob_datum_funcs.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
struct ObColDesc;
}
}
namespace blocksstable
{

// Stable comparison/schema contract used by Query and implemented by Storage.
struct ObStorageDatumCmpFunc
{
public:
  ObStorageDatumCmpFunc(common::ObCmpFunc &cmp_func) : cmp_func_(cmp_func) {}
  ObStorageDatumCmpFunc() = default;
  ~ObStorageDatumCmpFunc() = default;
  int compare(const ObStorageDatum &left, const ObStorageDatum &right, int &cmp_ret) const;
  OB_INLINE const common::ObCmpFunc &get_cmp_func() const { return cmp_func_; }
  TO_STRING_KV(K_(cmp_func));
private:
  common::ObCmpFunc cmp_func_;
};

typedef storage::ObFixedMetaObjArray<ObStorageDatumCmpFunc> ObStoreCmpFuncs;
typedef storage::ObFixedMetaObjArray<common::ObHashFunc> ObStoreHashFuncs;

struct ObStorageDatumUtils
{
public:
  ObStorageDatumUtils();
  ~ObStorageDatumUtils();
  int init(const common::ObIArray<share::schema::ObColDesc> &col_descs,
           const int64_t schema_rowkey_cnt,
           common::ObIAllocator &allocator,
           const bool skip_multi_version_cols = false);
  int init(const common::ObIArray<share::schema::ObColDesc> &col_descs,
           const int64_t schema_rowkey_cnt,
           const int64_t arr_buf_len,
           char *arr_buf);
  int assign(const ObStorageDatumUtils &other_utils, common::ObIAllocator &allocator);
  void reset();
  OB_INLINE bool is_valid() const
  {
    return is_inited_ && cmp_funcs_.count() >= rowkey_cnt_ && hash_funcs_.count() >= rowkey_cnt_;
  }
  OB_INLINE int64_t get_rowkey_count() const { return rowkey_cnt_; }
  OB_INLINE const ObStoreCmpFuncs &get_cmp_funcs() const { return cmp_funcs_; }
  OB_INLINE const ObStoreHashFuncs &get_hash_funcs() const { return hash_funcs_; }
  OB_INLINE const common::ObHashFunc &get_ext_hash_funcs() const { return ext_hash_func_; }
  int64_t get_deep_copy_size() const;
  TO_STRING_KV(K_(rowkey_cnt), K_(is_inited));
private:
  int transform_multi_version_col_desc(
      const common::ObIArray<share::schema::ObColDesc> &col_descs,
      const int64_t schema_rowkey_cnt,
      common::ObIArray<share::schema::ObColDesc> &mv_col_descs);
  int inner_init(
      const common::ObIArray<share::schema::ObColDesc> &mv_col_descs,
      const int64_t mv_rowkey_col_cnt);
private:
  int32_t rowkey_cnt_;
  ObStoreCmpFuncs cmp_funcs_;
  ObStoreHashFuncs hash_funcs_;
  common::ObHashFunc ext_hash_func_;
  bool is_inited_;
  DISALLOW_COPY_AND_ASSIGN(ObStorageDatumUtils);
};

} // namespace blocksstable
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_BLOCKSSTABLE_OB_STORAGE_DATUM_UTILS_H_
