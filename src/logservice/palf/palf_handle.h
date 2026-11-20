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

#ifndef OCEANBASE_LOGSERVICE_PALF_HANDLE_
#define OCEANBASE_LOGSERVICE_PALF_HANDLE_
#include "common/ob_role.h"
#include "share/log/palf/lsn.h"
#include "palf_handle_impl.h"
#include "palf_iterator.h"
namespace oceanbase
{
namespace share
{
class SCN;
}
namespace palf
{
class PalfAppendOptions;
class PalfFSCb;
class PalfHandle
{
public:
  friend class PalfEnv;
  friend class PalfEnvImpl;
  friend class PalfHandleGuard;
  PalfHandle();
  PalfHandle(const PalfHandle &rhs);
  ~PalfHandle();
  bool is_valid() const;

  // @brief copy-assignment operator
  // NB: we wouldn't destroy 'this', therefor, if 'this' is valid,
  // after operator=, PalfHandleImpl and Callback have leaked.
  PalfHandle& operator=(const PalfHandle &rhs);
  // @brief move-assignment operator
  PalfHandle& operator=(PalfHandle &&rhs);
  bool operator==(const PalfHandle &rhs) const;
  //================ File access related interfaces =======================
  int append(const PalfAppendOptions &opts,
             const void *buffer,
             const int64_t nbytes,
             const share::SCN &ref_scn,
             LSN &lsn,
             share::SCN &scn);
  int append_imported_groups(const ImportedGroups &groups);

  // iter->next returns the value written by the append call, and will not carry the header information added by Palf in the returned buf
  //           The returned value does not include unconfirmed logs
  //
  // When constructing Iterator at specified start_lsn, iter will automatically determine based on PalfHandle::accepted_end_lsn
  // Determine the end position of the iteration, this end position will be automatically updated (i.e., after returning OB_ITER_END again
  // There is a possibility that iter->next() returns a valid value)
  //
  // The lifecycle of PalfBufferIterator is managed by the caller
  // The caller needs to ensure that the iter associated PalfHandle is not accessed after it is closed
  // This Iterator will internally cache a large Buffer
  int seek(const LSN &lsn, PalfBufferIterator &iter);

  int seek(const LSN &lsn, PalfGroupBufferIterator &iter);

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
  int seek(const share::SCN &scn, PalfGroupBufferIterator &iter);
  int seek(const share::SCN &scn, PalfBufferIterator &iter);

  // @desc: query coarse lsn by scn, that means there is a LogGroupEntry in disk,
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
  // Return the position information of the earliest readable log in the file
  int get_begin_lsn(LSN &lsn) const;
  // return the max recyclable point of Palf
  int get_base_lsn(LSN &lsn) const;

  // PalfBaseInfo include the 'base_lsn' and the 'prev_log_info' of sliding window.
  // @param[in] const LSN&, base_lsn of ls.
  // @param[out] PalfBaseInfo&, palf_base_info
  int get_base_info(const LSN &lsn,
                    PalfBaseInfo &palf_base_info);
  // Return the position after the last confirmed log
  // In the scenario without new writes, the returned end_lsn is not readable
  int get_end_lsn(LSN &lsn) const;
  int get_end_scn(share::SCN &scn) const;
  int get_max_lsn(LSN &lsn) const;
  int get_max_scn(share::SCN &scn) const;
  int get_access_mode_ref_scn(AccessMode &access_mode,
                              SCN &ref_scn) const;

	//================= Callback function registration ===========================
  // @brief: register a callback to PalfHandleImpl, and do something in
  // this callback when file size has changed.
  // NB: not thread safe
  int register_file_size_cb(PalfFSCb *fs_cb);

  // @brief: unregister a callback from PalfHandleImpl
  // NB: not thread safe
  int unregister_file_size_cb();

	//================= Dependency function registration ===========================
  int stat(PalfStat &palf_stat) const;


  int diagnose(PalfDiagnoseInfo &diagnose_info) const;

  TO_STRING_KV(KP(palf_handle_impl_), KP(fs_cb_));
private:
  palf::PalfHandleImpl *palf_handle_impl_;
  palf::PalfFSCbNode *fs_cb_;
};
} // end namespace oceanbase
} // end namespace palf
#endif
