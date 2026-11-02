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

#include "lib/string/ob_string.h"
#include "lib/container/ob_array.h"
#include "lib/container/ob_se_array.h"
#include "common/object/ob_object.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
namespace sql
{

enum class FileFormat {
  CSV = 0,
  JSONL,
  PARQUET,
  MAX_FORMAT
};

struct FileScanOption
{
  FileFormat format_;
  bool has_header_;
  char field_delimiter_;
  char quote_char_;
  char escape_char_;
  int64_t sample_size_;  // -1 for full inference
  
  FileScanOption()
    : format_(FileFormat::CSV),
      has_header_(true),
      field_delimiter_(','),
      quote_char_('"'),
      escape_char_('\\'),
      sample_size_(-1)
  {}
};

struct ColumnMeta
{
  common::ObString name_;
  common::ObObjType type_;
  int64_t max_length_;
  bool nullable_;
  
  ColumnMeta()
    : name_(),
      type_(common::ObVarcharType),
      max_length_(0),
      nullable_(true)
  {}
};

class FileScanUtils
{
public:
  static int infer_schema_from_csv(const common::ObString &file_path,
                                    const FileScanOption &option,
                                    common::ObIArray<ColumnMeta> &schema);
  
  static int infer_schema_from_jsonl(const common::ObString &file_path,
                                      const FileScanOption &option,
                                      common::ObIArray<ColumnMeta> &schema);
  
  static int infer_schema_from_parquet(const common::ObString &file_path,
                                        const FileScanOption &option,
                                        common::ObIArray<ColumnMeta> &schema);
  
  static int widen_type(common::ObObjType type1, common::ObObjType type2, common::ObObjType &result);
  
  static bool is_valid_path(const common::ObString &path, const common::ObString &secure_path);
  
  static int check_file_accessible(const common::ObString &file_path);
};

} // namespace sql
} // namespace oceanbase
