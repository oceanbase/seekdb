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

#ifndef OCEANBASE_STORAGE_LS_SYNC_TABLET_SEQ_HANDLER_
#define OCEANBASE_STORAGE_LS_SYNC_TABLET_SEQ_HANDLER_

#include "common/ob_tablet_id.h"
#include "share/scn.h"
#include "logservice/ob_log_base_type.h"

namespace oceanbase
{

namespace storage
{
class ObLS;

class ObLSSyncTabletSeqHandler : public logservice::ObIReplaySubHandler,
                                 public logservice::ObIRoleChangeSubHandler,
                                 public logservice::ObICheckpointSubHandler
{
public:
  ObLSSyncTabletSeqHandler() : is_inited_(false), ls_(nullptr) {}
  ~ObLSSyncTabletSeqHandler() { reset(); }

public:
  int init(ObLS *ls);
  void reset();
  // for replay
  int replay(const void *buffer,
             const int64_t nbytes,
             const palf::LSN &lsn,
             const share::SCN &scn) override final;

  // for role change
  void switch_to_follower_forcedly() override final;
  int switch_to_leader() override final;
  int switch_to_follower_gracefully() override final;
  int resume_leader() override final;

  // for checkpoint
  int flush(share::SCN &scn) override final;
  share::SCN get_rec_scn() override final;

private:
  bool is_inited_;
  ObLS *ls_;

};

} // storage
} // oceanbase

#endif // OCEANBASE_STORAGE_LS_SYNC_TABLET_SEQ_HANDLER_
