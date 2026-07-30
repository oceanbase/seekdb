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

#ifndef OB_UNIQUE_INDEX_ROW_TRANSFORMER_H_
#define OB_UNIQUE_INDEX_ROW_TRANSFORMER_H_

#include "common/row/ob_row.h"

namespace oceanbase
{
namespace share
{

class ObUniqueIndexRowTransformer
{
public:
  static int check_need_shadow_columns(
      const common::ObNewRow &row,
      const int64_t unique_key_cnt,
      const common::ObIArray<int64_t> *projector,
      bool &need_shadow_columns);
  static int convert_to_unique_index_row(
      const common::ObNewRow &row,
      const int64_t unique_key_cnt,
      const int64_t shadow_column_cnt,
      const common::ObIArray<int64_t> *projector,
      bool &need_shadow_columns,
      common::ObNewRow &result_row,
      const bool need_copy_cell = false);
private:
  static int check_mysql_need_shadow_columns(
      const common::ObNewRow &row,
      const int64_t unique_key_cnt,
      const common::ObIArray<int64_t> *projector,
      bool &need_shadow_columns);
};

template<typename T>
class ObUniqueIndexRowTransformerV2
{
public:
  static int check_need_shadow_columns(
      const T &row,
      const int64_t unique_key_cnt,
      const common::ObIArray<int64_t> *projector,
      bool &need_shadow_columns);
  static int convert_to_unique_index_row(
      const int64_t unique_key_cnt,
      const int64_t shadow_column_cnt,
      const common::ObIArray<int64_t> *projector,
      T &row,
      bool &need_shadow_columns);
private:
  static int check_mysql_need_shadow_columns(
      const T &row,
      const int64_t unique_key_cnt,
      const common::ObIArray<int64_t> *projector,
      bool &need_shadow_columns);
};

template<typename T>
int ObUniqueIndexRowTransformerV2<T>::check_need_shadow_columns(
    const T &row,
    const int64_t unique_key_cnt,
    const common::ObIArray<int64_t> *projector,
    bool &need_shadow_columns)
{
  int ret = common::OB_SUCCESS;
  const int64_t cell_cnt = row.get_count();
  need_shadow_columns = false;
  if (OB_UNLIKELY(!row.is_valid() || unique_key_cnt <= 0 || unique_key_cnt > cell_cnt)) {
    ret = common::OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN, "invalid arguments", K(ret), K(row), K(unique_key_cnt));
  } else if (OB_FAIL(check_mysql_need_shadow_columns(row, unique_key_cnt, projector, need_shadow_columns))) {
    SHARE_LOG(WARN, "fail to check mysql need shadow columns", K(ret));
  }
  return ret;
}

template<typename T>
int ObUniqueIndexRowTransformerV2<T>::check_mysql_need_shadow_columns(
    const T &row,
    const int64_t unique_key_cnt,
    const common::ObIArray<int64_t> *projector,
    bool &need_shadow_columns)
{
  int ret = common::OB_SUCCESS;
  const int64_t cell_cnt = row.get_count();
  need_shadow_columns = false;
  if (OB_UNLIKELY(!row.is_valid() || unique_key_cnt <= 0 || unique_key_cnt > cell_cnt)) {
    ret = common::OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN, "invalid arguments", K(ret), K(row), K(unique_key_cnt));
  } else {
    bool rowkey_has_null = false;
    // mysql compatible: when at least one column of unique key is null, fill the shadow columns
    for (int64_t i = 0; OB_SUCC(ret) && i < unique_key_cnt && !rowkey_has_null; ++i) {
      const int64_t idx = NULL == projector ? i : projector->at(i);
      if (idx >= cell_cnt) {
        ret = common::OB_ERR_UNEXPECTED;
        SHARE_LOG(WARN, "error unexpected, idx exceed the row cells count", K(ret), K(idx), K(row));
      } else {
        rowkey_has_null = row.get_cell(idx).is_null();
      }
    }
    need_shadow_columns = rowkey_has_null;
  }
  return ret;
}

template <typename T>
int ObUniqueIndexRowTransformerV2<T>::convert_to_unique_index_row(
    const int64_t unique_key_cnt,
    const int64_t shadow_column_cnt,
    const common::ObIArray<int64_t> *projector,
    T &row,
    bool &need_shadow_columns)
{
  int ret = common::OB_SUCCESS;
  need_shadow_columns = false;
  if (OB_UNLIKELY(!row.is_valid() || unique_key_cnt <= 0 || shadow_column_cnt <= 0)) {
    ret = common::OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN, "invalid arguments", K(ret), K(row), K(unique_key_cnt), K(shadow_column_cnt));
  } else if (OB_FAIL(check_need_shadow_columns(row, unique_key_cnt, projector, need_shadow_columns))) {
    SHARE_LOG(WARN, "fail to check need shadow columns", K(ret));
  } else {
    const int64_t cell_cnt = row.get_count();
    for (int64_t i = unique_key_cnt; OB_SUCC(ret) && i < unique_key_cnt + shadow_column_cnt; ++i) {
      const int64_t idx = NULL == projector ? i : projector->at(i);
      if (idx >= cell_cnt) {
        ret = common::OB_ERR_UNEXPECTED;
        SHARE_LOG(WARN, "error unexpected, idx is not valid", K(idx), K(row));
      } else {
        if (!need_shadow_columns) {
          row.get_cell(i).set_null();
        }
      }
    }
  }
  return ret;
}

}  // end namespace share
}  // end namespace oceanbase

#endif  // OB_UNIQUE_INDEX_ROW_TRANSFORMER_H_
