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

#ifndef OCEABASE_STORAGE_TABLET_TRANSFER_INFO_
#define OCEABASE_STORAGE_TABLET_TRANSFER_INFO_

#include "share/ob_define.h"
#include "lib/utility/ob_unify_serialize.h"
#include "share/scn.h"

namespace oceanbase
{
namespace storage
{

struct ObTabletTransferInfo final
{
  OB_UNIS_VERSION(1);
public:
  ObTabletTransferInfo();
  ~ObTabletTransferInfo() = default;
  int init();
  int init(
      const share::ObLSID &ls_id,
      const share::SCN &transfer_start_scn,
      const int64_t transfer_seq);
  void reset();
  bool is_valid() const;
  bool has_transfer_table() const;
  void reset_transfer_table();

  TO_STRING_KV(K_(ls_id), K_(transfer_start_scn), K_(transfer_seq), K_(has_transfer_table));
public:
  share::ObLSID ls_id_;
  share::SCN transfer_start_scn_;
  int64_t transfer_seq_;
  bool has_transfer_table_;
  static const int64_t TRANSFER_INIT_SEQ = 0;
private:
  static const int64_t TRANSFER_INIT_LS_ID = 0;
};


}
}
#endif
