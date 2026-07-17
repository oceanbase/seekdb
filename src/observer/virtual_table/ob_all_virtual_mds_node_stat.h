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

#ifndef OB_ALL_VIRTUAL_MDS__NODE_STAT_H
#define OB_ALL_VIRTUAL_MDS__NODE_STAT_H

#include "lib/container/ob_tuple.h"
#include "ob_tablet_id.h"
#include "observer/virtual_table/ob_virtual_table_scanner_iterator.h"
namespace oceanbase
{
namespace storage
{
namespace mds
{
struct MdsNodeInfoForVirtualTable;
}
}
namespace observer
{

class ApplyOnTabletOp;

class ObAllVirtualMdsNodeStat : public common::ObVirtualTableScannerIterator
{
  friend class ApplyOnTabletOp;
public:
  ObAllVirtualMdsNodeStat() = default;
  virtual int inner_get_next_row(common::ObNewRow *&row) override;
  TO_STRING_KV(K_(tablet_ranges), K_(tablet_points))
private:
  int convert_node_info_to_row_(const storage::mds::MdsNodeInfoForVirtualTable &node_info,
                                char *buffer,
                                const int64_t buffer_size,
                                common::ObNewRow &row);
  int get_primary_key_ranges_();
  int get_tablet_info_(ObLS &ls, const ObFunction<int(ObTablet &)> &apply_on_tablet_op);
  template <typename T>
  bool judege_in_ranges(const T &element, const ObArray<ObTuple<T, T>> &element_ranges) {
    bool in_range = false;
    for (auto &range : element_ranges) {
      if (element >= range.template element<0>() && element <= range.template element<1>()) {
        in_range = true;
        break;
      }
    }
    return in_range;
  }
  int get_mds_table_handle_(ObTablet &tablet,
                            mds::MdsTableHandle &handle,
                            const bool create_if_not_exist);
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualMdsNodeStat);
  ObArray<ObTuple<common::ObTabletID, common::ObTabletID>> tablet_ranges_;
  ObArray<common::ObTabletID> tablet_points_;
};

} // observer
} // oceanbase
#endif
