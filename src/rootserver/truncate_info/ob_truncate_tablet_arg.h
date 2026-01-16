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
#ifndef OB_ROOTSERVER_TRUNCATE_INFO_TRUNCATE_TABLET_ARG_H_
#define OB_ROOTSERVER_TRUNCATE_INFO_TRUNCATE_TABLET_ARG_H_

#include "share/ob_ls_id.h"
#include "common/ob_tablet_id.h"
#include "storage/truncate_info/ob_truncate_info.h"

namespace oceanbase
{
namespace rootserver
{

struct ObTruncateTabletArg
{
public:
  ObTruncateTabletArg()
    : version_(TRUNCATE_INFO_ARG_VERSION_V1),
      reserved_(0),
      ls_id_(),
      index_tablet_id_(),
      truncate_info_()
  {}
  ~ObTruncateTabletArg() { destroy(); }
  void destroy() {
    ls_id_.reset();
    index_tablet_id_.reset();
    truncate_info_.destroy();
  }
  bool is_valid() const { return ls_id_.is_valid() && index_tablet_id_.is_valid() && truncate_info_.is_valid(); }

  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const;
  int deserialize(
      common::ObIAllocator &allocator,
      const char *buf,
      const int64_t data_len,
      int64_t &pos);
  int64_t get_serialize_size() const;
  TO_STRING_KV(K_(version), K_(ls_id), K_(index_tablet_id), K_(truncate_info));
  static const int64_t TRUNCATE_INFO_ARG_VERSION_V1 = 1;
  static const int32_t TIA_ONE_BYTE = 8;
  static const int32_t TIA_RESERVED_BITS = 56;
  union {
    uint64_t info_;
    struct
    {
      uint64_t version_     : TIA_ONE_BYTE;
      uint64_t reserved_    : TIA_RESERVED_BITS;
    };
  };
  share::ObLSID ls_id_;
  ObTabletID index_tablet_id_;
  storage::ObTruncateInfo truncate_info_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTruncateTabletArg);
};

} // namespace rootserver
} // namespace oceanbase

#endif // OB_ROOTSERVER_TRUNCATE_INFO_TRUNCATE_TABLET_ARG_H_
