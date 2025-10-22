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

#ifndef OCEANBASE_STORAGE_TABLET_OB_I_TABLET_MDS_CUSTOMIZED_INTERFACE_H
#define OCEANBASE_STORAGE_TABLET_OB_I_TABLET_MDS_CUSTOMIZED_INTERFACE_H
#include "storage/tablet/ob_i_tablet_mds_interface.h"

namespace oceanbase
{
namespace storage
{

// ObITabletMdsCustomizedInterface is for MDS users to customize their own wrapper needs.
// All interfaces in ObITabletMdsInterface are type independent, but some users may want read MDS with more operations.
// They want centralize their requirements into a common document, so we define ObITabletMdsCustomizedInterface here.
class ObITabletMdsCustomizedInterface : public ObITabletMdsInterface
{
public:
  // customized get_latest_committed
  int get_ddl_data(ObTabletBindingMdsUserData &ddl_data);
  int get_autoinc_seq(share::ObTabletAutoincSeq &inc_seq, ObIAllocator &allocator);

  // customized get_latest
  int get_latest_split_data(ObTabletSplitMdsUserData &data,
                            mds::MdsWriter &writer,
                            mds::TwoPhaseCommitState &trans_stat,
                            share::SCN &trans_version,
                            const int64_t read_seq = 0) const;
  int get_latest_autoinc_seq(ObTabletAutoincSeq &data,
                             ObIAllocator &allocator,
                             mds::MdsWriter &writer,
                             mds::TwoPhaseCommitState &trans_stat,
                             share::SCN &trans_version,
                             const int64_t read_seq = 0) const;
  int get_ddl_complete(const share::SCN &snapshot,
                       ObIAllocator &allocator,
                       ObTabletDDLCompleteMdsUserData &data,
                       const int64_t timeout = ObTabletCommon::DEFAULT_GET_TABLET_DURATION_US) const;

  int get_latest_committed_tablet_status(ObTabletCreateDeleteMdsUserData &data) const;
  int get_latest_binding_info(ObTabletBindingMdsUserData &data,
    mds::MdsWriter &writer,
    mds::TwoPhaseCommitState &trans_stat,
    share::SCN &trans_version) const;
  // customized get_snapshot
  // TODO (jiahua.cjh): move interface from ob_i_tablet_mds_interface to this file
};

struct ReadDDLCompleteOp
{
  ReadDDLCompleteOp(ObIAllocator &allocator, ObTabletDDLCompleteMdsUserData &ddl_complete)
      : allocator_(allocator), ddl_complete_(ddl_complete) {}
  int operator()(const ObTabletDDLCompleteMdsUserData &ddl_complete)
  {
    return ddl_complete_.assign(allocator_, ddl_complete);
  }
  ObIAllocator &allocator_;
  ObTabletDDLCompleteMdsUserData &ddl_complete_;
};

}
}

#endif
