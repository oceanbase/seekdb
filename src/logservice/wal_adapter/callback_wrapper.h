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

#ifndef OCEANBASE_LOGSERVICE_WAL_ADAPTER_CALLBACK_WRAPPER_
#define OCEANBASE_LOGSERVICE_WAL_ADAPTER_CALLBACK_WRAPPER_
#include "lib/list/ob_dlink_node.h"
#include "lib/list/ob_dlist.h"
#include "lib/ob_errno.h"
#include "lib/lock/ob_spin_lock.h"
#include "callback.h"
namespace oceanbase
{
namespace wal
{

struct FsyncCallbackNode : public common::ObDLinkBase<FsyncCallbackNode>
{
  FsyncCallbackNode(FsyncCallback *fs_cb) : fs_cb_(fs_cb) {}
  FsyncCallback *fs_cb_;
  TO_STRING_KV(KP(fs_cb_), KP(prev_), KP(next_));
};

class FsyncCallbackWrapper
{
public:
  FsyncCallbackWrapper();
  ~FsyncCallbackWrapper();
  virtual int add_cb_impl(FsyncCallbackNode *cb_impl);
  virtual void del_cb_impl(FsyncCallbackNode *cb_impl);
  virtual int update_end_lsn(const LSN &end_lsn, const share::SCN &end_scn);
private:
  // The head of list
  ObDList<FsyncCallbackNode> list_;
  ObSpinLock lock_;
};

} // end namespace wal
} // end namespace oceanbase
#endif
