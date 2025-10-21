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

#include "storage/tablet/ob_tablet_complex_addr.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace storage
{
template <>
int64_t ObTabletComplexAddr<mds::MdsDumpKV>::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;

  if (OB_ISNULL(buf) || buf_len <= 0) {
    // do nothing
  } else {
    databuff_printf(buf, buf_len, pos, "{ptr:%p", static_cast<void*>(ptr_));
    if (nullptr != ptr_) {
      databuff_printf(buf, buf_len, pos, ", dump_node:");
      const mds::MdsDumpNode &dump_node = ptr_->v_;
      dump_node.simple_to_string(buf, buf_len, pos);
    }
    databuff_print_json_kv_comma(buf, buf_len, pos, "addr", addr_);
    databuff_printf(buf, buf_len, pos, "}");
  }
  return pos;
}

template <>
int64_t ObTabletComplexAddr<share::ObTabletAutoincSeq>::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;

  if (OB_ISNULL(buf) || buf_len <= 0) {
    // do nothing
  } else {
    databuff_printf(buf, buf_len, pos, "{ptr:%p", static_cast<void*>(ptr_));
    if (nullptr != ptr_) {
      databuff_printf(buf, buf_len, pos, ", auto_inc_seq:");
      databuff_print_obj(buf, buf_len, pos, *ptr_);
    }
    databuff_print_json_kv_comma(buf, buf_len, pos, "addr", addr_);
    databuff_printf(buf, buf_len, pos, "}");
  }
  return pos;
}

template <>
int64_t ObTabletComplexAddr<ObTabletDumpedMediumInfo>::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;

  if (OB_ISNULL(buf) || buf_len <= 0) {
    // do nothing
  } else {
    databuff_printf(buf, buf_len, pos, "{ptr:%p", static_cast<void*>(ptr_));
    if (nullptr != ptr_) {
      databuff_printf(buf, buf_len, pos, ", dumped_medium_info:");
      ptr_->simple_to_string(buf, buf_len, pos);
    }
    databuff_print_json_kv_comma(buf, buf_len, pos, "addr", addr_);
    databuff_printf(buf, buf_len, pos, "}");
  }
  return pos;
}
} // namespace storage
} // namespace oceanbase
