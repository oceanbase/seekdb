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
#include "common/datum/datum_copy_plan.h"

namespace oceanbase
{
namespace sql
{
using namespace common;

namespace
{
// The pilot accepts exactly one complete scalar type. Its immutable descriptor
// and prepared copy plan can be shared; the plan never contains execution
// values, borrowed payload addresses, rowkey hashes or group information.
class BigintPointLayout final
{
public:
  BigintPointLayout() : type_(), init_ret_(init_type(type_)), copy_plan_(type_) {}
  BigintPointLayout(const BigintPointLayout &) = delete;
  BigintPointLayout &operator=(const BigintPointLayout &) = delete;

  int status() const { return init_ret_; }
  const SqlType &type() const { return type_; }
  const DatumCopyPlan &copy_plan() const { return copy_plan_; }

private:
  static int init_type(SqlType &type)
  {
    SqlTypeMeta meta;
    meta.set_int();
    return SqlType::from_legacy(meta, ObAccuracy(20, 0), type);
  }

  // Declaration order matters: finish the type before the plan borrows it.
  SqlType type_;
  const int init_ret_;
  const DatumCopyPlan copy_plan_;
};

const BigintPointLayout &bigint_point_layout()
{
  // C++ synchronizes initialization before concurrent callers observe the
  // object. Its address and the plan's borrowed type remain stable for life.
  static const BigintPointLayout layout;
  return layout;
}
}

NativePointGetInput::NativePointGetInput()
  : values_(), storage_datums_(), legacy_values_(),
    cached_rowkey_(), key_(), lookup_keys_(1, &key_)
{}

int NativePointGetInput::init(const Datum (&values)[COLUMN_COUNT], int64_t group_id)
{
  int ret = OB_SUCCESS;
  // For the fixed signed BIGINT type this is the complete scalar-shape
  // preflight. Do it for every input before replacing any published value.
  for (int64_t i = 0; OB_SUCC(ret) && i < COLUMN_COUNT; ++i) {
    if (!values[i].is_present() || values[i].length() != 0
        || values[i].reference_flags() != Datum::NoReferenceFlags) {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  const BigintPointLayout *layout = nullptr;
  if (OB_SUCC(ret)) {
    layout = &bigint_point_layout();
    ret = layout->status();
  }
  if (OB_SUCC(ret)) {
    for (int64_t i = 0; i < COLUMN_COUNT; ++i) { values_[i] = values[i]; }
    for (int64_t i = 0; OB_SUCC(ret) && i < COLUMN_COUNT; ++i) {
      ObDatum borrowed;
      // The type and scalar shape were established above, so this creates
      // views directly into stable members, never into the caller's array.
      if (OB_FAIL(layout->copy_plan().borrow_legacy(values_[i], borrowed))) {
      } else {
        storage_datums_[i].shallow_copy_from_datum(borrowed);
        legacy_values_[i] = DatumValue(borrowed, layout->type().legacy_meta());
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(cached_rowkey_.assign(legacy_values_, COLUMN_COUNT))) {
    } else if (OB_SUCC(ret)) {
      cached_rowkey_.set_group_idx(group_id);
      key_.datums_ = storage_datums_;
      key_.count_ = COLUMN_COUNT;
      key_.group_id_ = group_id;
      key_.cached_rowkey_ = &cached_rowkey_;
      // Fixed IArray view: publishing one key needs no array/payload allocation.
      lookup_keys_.init(1, &key_, 1);
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
