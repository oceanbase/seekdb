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

#pragma once

#include "lib/container/ob_iarray.h"
#include "share/table/ob_table_load_array.h"
#include "sql/session/ob_sql_session_mgr.h"

namespace oceanbase
{
namespace blocksstable
{
class ObStorageDatum;
class ObDatumRange;
class ObDatumRowkey;
class ObDatumRow;
}  // namespace blocksstable
namespace common
{
class ObAddr;
class ObString;
class ObObj;
class ObStoreRowkey;
class ObStoreRange;
class ObNewRow;
}  // namespace common
namespace table
{
  class ObTableApiCredential;
} // namespace table
namespace sql
{
class ObSQLSessionInfo;
} // namespace sql
namespace observer
{

class ObTableLoadUtils
{
public:
  template<class T>
  static int deep_copy(const T &src, T &dest, common::ObIAllocator &allocator);
  static int deep_copy(const common::ObString &src, common::ObString &dest, common::ObIAllocator &allocator);
  static int deep_copy(const common::ObObj &src, common::ObObj &dest, common::ObIAllocator &allocator);
  static int deep_copy(const common::ObStoreRowkey &src, common::ObStoreRowkey &dest, common::ObIAllocator &allocator);
  static int deep_copy(const blocksstable::ObDatumRowkey &src, blocksstable::ObDatumRowkey &dest, common::ObIAllocator &allocator);
  static int deep_copy(const sql::ObSQLSessionInfo &src, sql::ObSQLSessionInfo &dest, common::ObIAllocator &allocator);

  template<class T>
  static int deep_copy(const table::ObTableLoadArray<T> &src, table::ObTableLoadArray<T> &dest, common::ObIAllocator &allocator);

  template<class T>
  static int deep_copy_transform(const table::ObTableLoadArray<T> &src, table::ObTableLoadArray<T> &dest, common::ObIAllocator &allocator, const common::ObIArray<int64_t> &idx_array);

  template<class T>
  static int deep_copy(const common::ObIArray<T> &src, table::ObTableLoadArray<T> &dest, common::ObIAllocator &allocator);
  static bool is_local_addr(const common::ObAddr &addr);
  static int create_session_info(sql::ObSQLSessionInfo *&session_info, sql::ObFreeSessionCtx &free_session_ctx);
  static void free_session_info(sql::ObSQLSessionInfo *session_info, const sql::ObFreeSessionCtx &free_session_ctx);
  static const int64_t CREDENTIAL_BUF_SIZE = 256;
};

template<class T>
int ObTableLoadUtils::deep_copy(const T &src, T &dest, common::ObIAllocator &allocator)
{
  dest = src;
  return common::OB_SUCCESS;
}

template<class T>
int ObTableLoadUtils::deep_copy(const table::ObTableLoadArray<T> &src, table::ObTableLoadArray<T> &dest, common::ObIAllocator &allocator)
{
  int ret = common::OB_SUCCESS;
  dest.reset();
  if (!src.empty()) {
    if (OB_FAIL(dest.create(src.count(), allocator))) {
      OB_LOG(WARN, "fail to create", KR(ret));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < src.count(); ++i) {
      if (OB_FAIL(deep_copy(src[i], dest[i], allocator))) {
        OB_LOG(WARN, "fail to deep copy", KR(ret));
      }
    }
  }
  return ret;
}

template<class T>
int ObTableLoadUtils::deep_copy_transform(const table::ObTableLoadArray<T> &src, table::ObTableLoadArray<T> &dest, common::ObIAllocator &allocator, const common::ObIArray<int64_t> &idx_array)
{
  int ret = common::OB_SUCCESS;
  dest.reset();
  if (!src.empty()) {
    int64_t col_count = idx_array.count();
    int64_t row_count = src.count() / col_count;
    if (OB_FAIL(dest.create(src.count(), allocator))) {
      OB_LOG(WARN, "fail to create", KR(ret));
    } else {
      const T* src_row = &src[0];
      T* dest_row = &dest[0];
      for (int64_t i = 0; OB_SUCC(ret) && (i < row_count); ++i) {
        for (int64_t j = 0; OB_SUCC(ret) && (j < col_count); ++j) {
          if (OB_FAIL(deep_copy(src_row[idx_array.at(j)], dest_row[j], allocator))) {
            OB_LOG(WARN, "fail to deep copy", KR(ret), K(row_count), K(col_count), K(i), K(j));
          }
        }
        src_row += col_count;
        dest_row += col_count;
      }
    }
  }
  return ret;
}

template<class T>
int ObTableLoadUtils::deep_copy(const common::ObIArray<T> &src, table::ObTableLoadArray<T> &dest, common::ObIAllocator &allocator)
{
  int ret = common::OB_SUCCESS;
  dest.reset();
  if (!src.empty()) {
    if (OB_FAIL(dest.create(src.count(), allocator))) {
      OB_LOG(WARN, "fail to create", KR(ret));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < src.count(); ++i) {
      if (OB_FAIL(deep_copy(src.at(i), dest[i], allocator))) {
        OB_LOG(WARN, "fail to deep copy", KR(ret));
      }
    }
  }
  return ret;
}

}  // namespace observer
}  // namespace oceanbase
