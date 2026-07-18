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

#ifndef OCEANBASE_STORAGE_OB_TABLET_MULTI_SOURCE_DATA
#define OCEANBASE_STORAGE_OB_TABLET_MULTI_SOURCE_DATA

#include <stdint.h>

#include "storage/memtable/ob_multi_source_data.h"
#include "storage/tablet/ob_tablet_common.h"
#include "storage/tablet/ob_tablet_status.h"
#include "storage/tx/ob_trans_define.h"
#include "share/scn.h"

namespace oceanbase
{
namespace storage
{
class ObTabletTxMultiSourceDataUnit : public memtable::ObIMultiSourceDataUnit
{
public:
  ObTabletTxMultiSourceDataUnit();
  virtual ~ObTabletTxMultiSourceDataUnit();
  ObTabletTxMultiSourceDataUnit(const ObTabletTxMultiSourceDataUnit &other);
public:
  virtual int deep_copy(const memtable::ObIMultiSourceDataUnit *src, ObIAllocator *allocator = nullptr) override;
  virtual void reset() override;
  virtual int64_t to_string(char *buf, const int64_t buf_len) const override;
  virtual bool is_valid() const override;
  virtual int64_t get_data_size() const override;
  virtual memtable::MultiSourceDataUnitType type() const override;
  virtual int set_scn(const share::SCN &scn) override;
public:
  int serialize(char *buf, const int64_t len, int64_t &pos) const;
  int deserialize(const char *buf, const int64_t len, int64_t &pos);
  int64_t get_serialize_size() const;

  bool is_in_tx() const;
public:
  int32_t version_;
  mutable int32_t length_; // length is assigned when serializing
  transaction::ObTransID tx_id_;
  share::SCN tx_scn_;
  ObTabletStatus tablet_status_;
  int64_t transfer_seq_;
  share::ObLSID transfer_ls_id_;
  share::SCN transfer_scn_;
private:
  static const int32_t TX_DATA_VERSION = 1;
};

inline memtable::MultiSourceDataUnitType ObTabletTxMultiSourceDataUnit::type() const
{
  return memtable::MultiSourceDataUnitType::TABLET_TX_DATA;
}

inline bool ObTabletTxMultiSourceDataUnit::is_in_tx() const
{
  return ObTabletCommon::FINAL_TX_ID != tx_id_;
}
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_TABLET_MULTI_SOURCE_DATA
