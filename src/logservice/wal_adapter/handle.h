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

#ifndef OCEANBASE_LOGSERVICE_WAL_ADAPTER_HANDLE_
#define OCEANBASE_LOGSERVICE_WAL_ADAPTER_HANDLE_
#include "common/ob_role.h"
#include "share/log/types/lsn.h"
#include "handle_impl.h"
#include "iterator.h"
namespace oceanbase
{
namespace share
{
class SCN;
}
namespace wal
{
class FsyncCallback;
class Handle
{
public:
  friend class Env;
  friend class Env;
  friend class HandleGuard;
  Handle();
  Handle(const Handle &rhs);
  ~Handle();
  bool is_valid() const;

  // @brief copy-assignment operator
  // NB: we wouldn't destroy 'this', therefor, if 'this' is valid,
  // after operator=, HandleImpl and Callback have leaked.
  Handle& operator=(const Handle &rhs);
  // @brief move-assignment operator
  Handle& operator=(Handle &&rhs);
  bool operator==(const Handle &rhs) const;
  //================ File access related interfaces =======================
  int append(const void *buffer,
             const int64_t nbytes,
             const share::SCN &ref_scn,
             LSN &lsn,
             share::SCN &scn);
  int append_imported_groups(const ImportedGroups &groups);

  // iter->next returns the value written by the append call, and will not carry the header information added by WAL in the returned buf
  //           The returned value does not include unconfirmed logs
  //
  // When constructing Iterator at specified start_lsn, iter will automatically determine based on Handle::accepted_end_lsn
  // Determine the end position of the iteration, this end position will be automatically updated (i.e., after returning OB_ITER_END again
  // There is a possibility that iter->next() returns a valid value)
  //
  // The lifecycle of EntryIterator is managed by the caller
  // The caller needs to ensure that the iter associated Handle is not accessed after it is closed
  // This Iterator will internally cache a large Buffer
  int seek(const LSN &lsn, EntryIterator &iter);

  int seek(const LSN &lsn, GroupIterator &iter);

  // @desc: seek a buffer(group buffer) iterator by scn, the first log A in iterator must meet
  // one of the following conditions:
  // 1. scn of log A equals to scn
  // 2. scn of log A is higher than scn and A is the first log which scn is higher
  // than scn in all committed logs
  // Note that this function may be time-consuming
  // @params [in] scn:
  //  @params [out] iter: group buffer iterator in which all logs's scn are higher than/equal to
  // scn
  // @return
  // - OB_SUCCESS
  // - OB_INVALID_ARGUMENT
  // - OB_ENTRY_NOT_EXIST: there is no log's scn is higher than scn
  // - OB_ERR_OUT_OF_LOWER_BOUND: scn is too old, log files may have been recycled
  // - others: bug
  int seek(const share::SCN &scn, GroupIterator &iter);
  int seek(const share::SCN &scn, EntryIterator &iter);

  // @desc: query coarse lsn by scn, that means there is a group record in disk,
  // its lsn and scn are result_lsn and result_scn, and result_scn <= scn.
  // Note that this function may be time-consuming
  // Note that result_lsn is a readable coarse lower bound for the located log.
  // @params [in] scn:
  // @params [out] result_lsn: the lower bound lsn which includes scn
  // @return
  // - OB_SUCCESS: locate_by_scn_coarsely success
  // - OB_INVALID_ARGUMENT
  // - OB_ENTRY_NOT_EXIST: there is no log in disk
  // - OB_ERR_OUT_OF_LOWER_BOUND: scn is too small, log files may have been recycled
  // - others: bug
  int locate_by_scn_coarsely(const share::SCN &scn, LSN &result_lsn);

  // @desc: query coarse scn by lsn, that means there is a log in disk,
  // its lsn and scn are result_lsn and result_scn, and result_lsn <= lsn.
  // Note that this function may be time-consuming
  // @params [in] lsn: lsn
  // @params [out] result_scn: the lower bound scn which includes lsn
  // - OB_SUCCESS; locate_by_lsn_coarsely success
  // - OB_INVALID_ARGUMENT
  // - OB_ERR_OUT_OF_LOWER_BOUND: lsn is too small, log files may have been recycled
  // - others: bug
  int locate_by_lsn_coarsely(const LSN &lsn, share::SCN &result_scn);
  // Advance the file's recyclable point
  int advance_base_lsn(const LSN &lsn);
  // return the max recyclable point of WAL
  int get_base_lsn(LSN &lsn) const;

  // BaseInfo include the 'base_lsn' and the 'prev_log_info' of sliding window.
  // @param[in] const LSN&, base_lsn of ls.
  // @param[out] BaseInfo&, wal_base_info
  int get_base_info(const LSN &lsn,
                    BaseInfo &wal_base_info);
  // Return the position after the last confirmed log
  // In the scenario without new writes, the returned end_lsn is not readable
  int get_end_lsn(LSN &lsn) const;
  int get_end_scn(share::SCN &scn) const;
	//================= Callback function registration ===========================
  // @brief: register a callback to HandleImpl, and do something in
  // this callback when file size has changed.
  // NB: not thread safe
  int register_file_size_cb(FsyncCallback *fs_cb);

  // @brief: unregister a callback from HandleImpl
  // NB: not thread safe
  int unregister_file_size_cb();

	//================= Dependency function registration ===========================
  int stat(Stat &wal_stat) const;
  TO_STRING_KV(KP(wal_handle_impl_.get()), KP(fs_cb_));
private:
  wal::HandleImplPtr wal_handle_impl_;
  wal::FsyncCallbackNode *fs_cb_;
};
} // end namespace oceanbase
} // end namespace wal
#endif
