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

#ifndef OCEANBASE_COMMON_OB_SQL_STRING_H_
#define OCEANBASE_COMMON_OB_SQL_STRING_H_

#include <stdarg.h>
#include "lib/allocator/ob_malloc.h"
#include "lib/string/ob_string.h"
#include "lib/utility/ob_print_utils.h"
namespace oceanbase
{
namespace common
{

// ObSqlString only used to concatenate SQL strings, for example:
//
//   ObSqlString sql;
//   int ret = sql.assign_fmt("SELECT c2 FROM %s WHERE c1 = %d", table_name, c1);
//
// ObSqlString add '\0' to the end of data, it's safe to use ptr() as C string.
//
class ObSqlString
{

public:
  explicit ObSqlString(const lib::ObLabel &label = ObModIds::OB_SQL_STRING);
  virtual ~ObSqlString();

  bool is_valid() const;
  void set_attr(const lib::ObMemAttr &attr) { allocator_.set_attr(attr); }
  void set_label(const lib::ObLabel &label)
  {
    lib::ObMemAttr attr;
    attr.label_ = label;
    allocator_.set_attr(attr);
 }
  void reset();
  void reuse();
  int reserve(const int64_t size);

  int append(const char *str);
  int append(const char *str, const int64_t len);
  int append(const ObString &str);
  int append_fmt(const char *fmt, ...) __attribute__((format(printf, 2, 3)));

  int assign(const char *str);
  int assign(const char *str, const int64_t len);
  int assign(const ObString &str);
  int assign(const ObSqlString &sql);
  int assign_fmt(const char *fmt, ...) __attribute__((format(printf, 2, 3)));

  const ObString string() const;
  const char *ptr() const { return data_; }
  int64_t length() const { return len_; }
  int64_t capacity() const { return data_size_ > 0 ? data_size_ - 1 : 0; }
  bool empty() const { return 0 == length(); }

  // Splice sql out of ObSqlString. example:
  //
  //   while (int64_t i = 0; i < sql.capacity(); ++i) {
  //      sql.ptr()[i] = ';';
  //   }
  //   ret = sql.set_length(sql.capacity());
  //
  // Dangerous api, caller's responsibility to ensure that modification occurs in [0, capacity())
  // and ptr() stable (no appendxxx(), assignxxx(), reset() called before set_length()).
  char *ptr() { return data_; }
  int set_length(const int64_t len);

  int64_t to_string(char *buf, const int64_t buf_len) const;
  
  int extend(const int64_t size);
private:
  int vappend(const char *fmt, va_list ap);

private:
  static const int64_t MAX_SQL_STRING_LEN = 512;
  char *data_;
  int64_t data_size_;
  int64_t len_;
  ObMalloc allocator_;

  DISALLOW_COPY_AND_ASSIGN(ObSqlString);
};

/**
 * @brief Escape the backtick characters inside a SQL identifier.
 *
 * A SQL identifier is delimited by backticks, so any backtick contained in the
 * identifier itself has to be doubled to keep the identifier intact:
 *
 *   key`id  ->  key``id
 *
 * The helper lives in oblib so that lower modules such as `share` can escape
 * identifiers without depending on the SQL upper layer.  The SQL layer keeps
 * its own entry point (ObSQLUtils::generate_new_name_with_escape_character) and
 * delegates here, so both layers always produce the same escaping.
 *
 * @param[in] allocator   allocator used for the escaped copy
 * @param[in] src         the raw identifier
 * @param[out] dst         the escaped identifier; when the source contains no
 *                         backtick it aliases the source and no memory is used
 * @return OB_SUCCESS on success
 */
inline int escape_sql_identifier(common::ObIAllocator &allocator,
                                 const common::ObString &src,
                                 common::ObString &dst)
{
  int ret = OB_SUCCESS;
  const common::ObString::obstr_size_t src_len = src.length();
  const char escape_character = '`';
  char *ptr = NULL;

  if (OB_ISNULL(src.ptr()) || OB_UNLIKELY(0 >= src_len)) {
    dst.assign(NULL, 0);
  } else if (NULL == src.find(escape_character)) {
    dst = src;
  } else {
    common::ObString::obstr_size_t dst_len = src_len;
    for (int64_t i = 0; i < src_len; ++i) {
      if (src[i] == escape_character) {
        ++dst_len;
      }
    }
    if (NULL == (ptr = static_cast<char *>(allocator.alloc(dst_len + 1)))) {
      dst = NULL;
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("allocate memory failed", K(ret), "size", dst_len);
    } else {
      int64_t tmp_dst_pos = 0;
      for (int64_t i = 0; i < src_len; i++) {
        if (src[i] == escape_character) {
          ptr[tmp_dst_pos++] = escape_character;
          ptr[tmp_dst_pos++] = escape_character;
        } else {
          ptr[tmp_dst_pos++] = src[i];
        }
      }
      if (tmp_dst_pos != dst_len) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        ptr[dst_len] = '\0';
        dst.assign_ptr(ptr, dst_len);
      }
    }
  }
  return ret;
}

} // end namespace common
} // end namespace oceanbase

#endif // OCEANBASE_COMMON_OB_SQL_STRING_H_
