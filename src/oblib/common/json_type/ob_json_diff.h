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

#ifndef OCEANBASE_SQL_OB_JSON_DIFF
#define OCEANBASE_SQL_OB_JSON_DIFF

#include "common/object/ob_object.h"
#include "common/json_type/ob_json_common.h"

namespace oceanbase {
namespace common {

// only support one element
enum class ObJsonDiffOp : uint8_t
{
  INVALID = 0,
  // same as json_replace(json, path, value)
  REPLACE = 1,
  // same as json_array_insert(json, path, value) if json is array element
  // same as json_insert(json, path, value)
  INSERT,
  // same as json_remove(json, path)
  REMOVE,
  RENAME
};

struct BinaryDiff
{
  int64_t offset;
  int64_t length;
};

class DeltaState;
class BinaryEdit;

class ObJsonBinUpdateCtx {
public:
  explicit ObJsonBinUpdateCtx(ObIAllocator *allocator);
  ObJsonBinUpdateCtx(const ObJsonBinUpdateCtx &) = delete;
  ObJsonBinUpdateCtx &operator=(const ObJsonBinUpdateCtx &) = delete;
  ~ObJsonBinUpdateCtx();
  int init_status() const { return delta_ == nullptr ? OB_ALLOCATE_MEMORY_FAILED : OB_SUCCESS; }

  ObStringBuffer& get_tmp_buffer() { tmp_buffer_.reuse(); return tmp_buffer_; }

  int current_data(ObString &data) const
  {
    return cursor_->get_data(data);
  }

  bool is_no_update() const;
  bool is_rebuild_all() const;
  void mark_rebuild_all();
  int64_t logical_diff_count() const;
  int64_t binary_diff_count() const;
  BinaryDiff binary_diff(int64_t index) const;
  bool intersects(uint64_t offset, uint64_t length) const;
  int64_t logical_serialize_size() const;
  int merge_edit(BinaryEdit &edit);
  int should_pack_diff(uint64_t total, uint64_t extension, uint64_t root_offset, bool &pack) const;
  int serialize_logical(char *buf, int64_t buf_len, int64_t &pos) const;
  int restore_delta(const char *buf, int64_t buf_len, const ObIArray<BinaryDiff> &physical);

  void set_lob_cursor(ObILobCursor *cursor) { cursor_ = cursor; }

  int record_inline_diff(ObJsonDiffOp op, uint8_t value_type, const ObString &path, uint8_t entry_var_type, const ObString &value);
  int record_remove_diff(const ObString &path);
  int record_diff(ObJsonDiffOp op, uint8_t value_type, const ObString &path, const ObString &value);
  int record_binary_diff(int64_t offset, int64_t len);

public:
  ObIAllocator *allocator_;
  ObILobCursor *cursor_;
  ObStringBuffer tmp_buffer_;


  TO_STRING_KV("rebuild_all", is_rebuild_all(),
               "logical_diff_count", logical_diff_count(),
               "binary_diff_count", binary_diff_count());

private:
  DeltaState *delta_;
};

} // namespace common
} // namespace oceanbase
#endif // OCEANBASE_SQL_OB_JSON_BIN
