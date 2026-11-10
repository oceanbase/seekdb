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

#include "sql/engine/table/native_point_get_input.h"
#include "common/datum/sql_type.h"

namespace oceanbase
{
namespace sql
{
using namespace common;

namespace
{
// The pilot accepts exactly one complete scalar type. Its immutable descriptor
// is shared; no execution value, address, hash or group is stored here.
class BigintPointLayout final
{
public:
  BigintPointLayout() : type_(), init_ret_(init_type(type_)) {}
  BigintPointLayout(const BigintPointLayout &) = delete;
  BigintPointLayout &operator=(const BigintPointLayout &) = delete;

  int status() const { return init_ret_; }
  const SqlType &type() const { return type_; }

private:
  static int init_type(SqlType &type)
  {
    SqlTypeMeta meta;
    meta.set_int();
    return SqlType::from_legacy(meta, ObAccuracy(20, 0), type);
  }

  SqlType type_;
  const int init_ret_;
};

const BigintPointLayout &bigint_point_layout()
{
  // C++ synchronizes initialization before concurrent callers observe the
  // object. Its address and complete type remain stable for life.
  static const BigintPointLayout layout;
  return layout;
}
}

NativePointGetInput::NativePointGetInput()
  : values_(), storage_datums_(), legacy_values_(),
    cached_rowkey_(), key_(), lookup_keys_(1, &key_)
{}

int NativePointGetInput::init(const int64_t (&values)[COLUMN_COUNT], int64_t group_id)
{
  const BigintPointLayout &layout = bigint_point_layout();
  int ret = layout.status();
  ObStoreRowkey prepared_rowkey;
  // assign() only binds the descriptor array and resets its hash; it does not
  // inspect the cells. Finish all fallible work before replacing any value.
  if (OB_SUCC(ret) && OB_FAIL(prepared_rowkey.assign(legacy_values_, COLUMN_COUNT))) {
  } else if (OB_SUCC(ret)) {
    prepared_rowkey.set_group_idx(group_id);
    for (int64_t i = 0; i < COLUMN_COUNT; ++i) {
      values_[i] = values[i];
      // The fixed storage sidecars borrow aligned int64_t bytes from this
      // owner. The generic rowkey values copy their I64 directly inline.
      const ObDatum borrowed(reinterpret_cast<const char *>(&values_[i]), sizeof(values_[i]), false);
      storage_datums_[i].shallow_copy_from_datum(borrowed);
      Datum native; native.set_int(values_[i]);
      legacy_values_[i] = DatumValue(native, layout.type().legacy_meta());
    }
    cached_rowkey_ = prepared_rowkey;
    key_.datums_ = storage_datums_;
    key_.count_ = COLUMN_COUNT;
    key_.group_id_ = group_id;
    key_.cached_rowkey_ = &cached_rowkey_;
    // Fixed IArray view: publishing one key needs no array/payload allocation.
    lookup_keys_.init(1, &key_, 1);
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
