/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#ifndef OCEANBASE_SQL_ENGINE_DML_OB_AI_COLUMN_META_H_
#define OCEANBASE_SQL_ENGINE_DML_OB_AI_COLUMN_META_H_

#include <stdlib.h>
#include <string.h>

#include "lib/string/ob_sql_string.h"
#include "lib/utility/ob_macro_utils.h"
#include "sql/resolver/ddl/ob_table_stmt.h"

namespace oceanbase
{
namespace sql
{

// AI column metadata is encoded into the comment of its hidden state column.
// DML can therefore read the metadata from the schema cache instead of issuing
// a SELECT against __all_ai_column for every statement.
constexpr char AI_COLUMN_META_PREFIX[] = "AIC1";
constexpr char AI_COLUMN_META_SEP = '\x1f';

inline int append_ai_column_meta_string(common::ObSqlString &out, const common::ObString &str)
{
  return out.append_fmt("%.*s", str.length(), str.ptr());
}

inline int encode_ai_column_meta(const ObAiColumnDef &def, common::ObSqlString &out)
{
  int ret = common::OB_SUCCESS;
  if (OB_FAIL(out.append_fmt("%s%c", AI_COLUMN_META_PREFIX, AI_COLUMN_META_SEP))) {
  } else if (OB_FAIL(append_ai_column_meta_string(out, def.column_name_))) {
  } else if (OB_FAIL(out.append_fmt("%c", AI_COLUMN_META_SEP))) {
  } else if (OB_FAIL(append_ai_column_meta_string(out, def.state_column_name_))) {
  } else if (OB_FAIL(out.append_fmt("%c", AI_COLUMN_META_SEP))) {
  } else if (OB_FAIL(append_ai_column_meta_string(out, def.request_column_name_))) {
  } else if (OB_FAIL(out.append_fmt("%c", AI_COLUMN_META_SEP))) {
  } else if (OB_FAIL(append_ai_column_meta_string(out, def.result_spec_column_name_))) {
  } else if (OB_FAIL(out.append_fmt("%c%ld%c%d%c", AI_COLUMN_META_SEP,
                                      def.max_retries_, AI_COLUMN_META_SEP,
                                      static_cast<int>(def.result_type_),
                                      AI_COLUMN_META_SEP))) {
  } else if (OB_FAIL(append_ai_column_meta_string(out, def.dep_column_names_))) {
  } else if (OB_FAIL(out.append_fmt("%c", AI_COLUMN_META_SEP))) {
  } else if (OB_FAIL(append_ai_column_meta_string(out, def.normalized_expr_))) {
  }
  return ret;
}

inline bool next_ai_column_meta_field(const common::ObString &meta,
                                      int32_t &pos,
                                      common::ObString &field)
{
  bool found = false;
  if (pos <= meta.length()) {
    const int32_t start = pos;
    while (pos < meta.length() && AI_COLUMN_META_SEP != meta.ptr()[pos]) {
      ++pos;
    }
    field.assign_ptr(meta.ptr() + start, pos - start);
    if (pos < meta.length()) {
      ++pos;
    }
    found = true;
  }
  return found;
}

inline int parse_ai_column_meta_int(const common::ObString &field, int64_t &value)
{
  int ret = common::OB_SUCCESS;
  char buf[32];
  if (field.length() <= 0 || field.length() >= static_cast<int32_t>(sizeof(buf))) {
    ret = common::OB_INVALID_ARGUMENT;
  } else {
    MEMCPY(buf, field.ptr(), field.length());
    buf[field.length()] = '\0';
    value = strtoll(buf, NULL, 10);
  }
  return ret;
}

inline int decode_ai_column_meta(const common::ObString &meta,
                                 common::ObIAllocator &alloc,
                                 ObAiColumnDef &def)
{
  int ret = common::OB_SUCCESS;
  const int32_t prefix_len = static_cast<int32_t>(strlen(AI_COLUMN_META_PREFIX));
  common::ObString field;
  int64_t max_retries = 0;
  int64_t result_type = 0;
  int32_t pos = prefix_len;
  if (meta.length() <= prefix_len
      || NULL == meta.ptr()
      || 0 != MEMCMP(meta.ptr(), AI_COLUMN_META_PREFIX, prefix_len)
      || AI_COLUMN_META_SEP != meta.ptr()[prefix_len]) {
    ret = common::OB_ERR_UNEXPECTED;
  } else if (FALSE_IT(pos = prefix_len + 1)) {
  } else if (!next_ai_column_meta_field(meta, pos, field)
             || OB_FAIL(common::ob_write_string(alloc, field, def.column_name_))) {
  } else if (!next_ai_column_meta_field(meta, pos, field)
             || OB_FAIL(common::ob_write_string(alloc, field, def.state_column_name_))) {
  } else if (!next_ai_column_meta_field(meta, pos, field)
             || OB_FAIL(common::ob_write_string(alloc, field, def.request_column_name_))) {
  } else if (!next_ai_column_meta_field(meta, pos, field)
             || OB_FAIL(common::ob_write_string(alloc, field, def.result_spec_column_name_))) {
  } else if (!next_ai_column_meta_field(meta, pos, field)
             || OB_FAIL(parse_ai_column_meta_int(field, max_retries))) {
  } else if (!next_ai_column_meta_field(meta, pos, field)
             || OB_FAIL(parse_ai_column_meta_int(field, result_type))) {
  } else if (!next_ai_column_meta_field(meta, pos, field)
             || OB_FAIL(common::ob_write_string(alloc, field, def.dep_column_names_))) {
  } else if (!next_ai_column_meta_field(meta, pos, field)
             || OB_FAIL(common::ob_write_string(alloc, field, def.normalized_expr_))) {
  } else {
    def.max_retries_ = static_cast<int64_t>(max_retries);
    def.result_type_ = static_cast<ObObjType>(result_type);
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_ENGINE_DML_OB_AI_COLUMN_META_H_
