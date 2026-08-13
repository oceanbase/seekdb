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

#define USING_LOG_PREFIX SERVER

#include "standby/ob_standby_palf_base_info.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log.h"
#include "logservice/ob_log_handler.h"
#include "logservice/palf/log_group_entry.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/ls/ob_ls.h"
#include "storage/tx_storage/ob_ls_service.h"

namespace oceanbase
{
namespace standby
{

ObFetchStandbyPalfBaseInfoArg::ObFetchStandbyPalfBaseInfoArg()
  : replay_start_scn_()
{
}

OB_SERIALIZE_MEMBER(ObFetchStandbyPalfBaseInfoArg, replay_start_scn_);

ObFetchStandbyPalfBaseInfoResult::ObFetchStandbyPalfBaseInfoResult()
  : palf_base_info_(),
    source_end_lsn_(),
    source_end_scn_(),
    located_log_(false)
{
}

OB_SERIALIZE_MEMBER(ObFetchStandbyPalfBaseInfoResult,
                    palf_base_info_,
                    source_end_lsn_,
                    source_end_scn_,
                    located_log_);

int ObStandbyPalfBaseInfoBuilder::build(
    const ObFetchStandbyPalfBaseInfoArg &arg,
    ObFetchStandbyPalfBaseInfoResult &result)
{
  int ret = OB_SUCCESS;
  if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid standby palf base info arg", K(ret), K(arg));
  } else {
    SERVER_MODULE_SCOPE {
      share::ObLSID ls_id(share::ObLSID::SYS_LS_ID);
      storage::ObLSService *ls_service = nullptr;
      storage::ObLS *ls = nullptr;
      logservice::ObLogHandler *log_handler = nullptr;
      palf::PalfBaseInfo scan_base_info;
      palf::LSN scan_lsn;
      palf::LSN curr_base_lsn;
      bool need_scan = true;

      if (OB_ISNULL(ls_service = share::server_service<storage::ObLSService>())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ls service should not be null", K(ret));
      } else if (OB_FAIL(ls_service->get_ls(ls))) {
        LOG_WARN("failed to get log stream", K(ret), K(ls_id), K(arg));
      } else if (OB_ISNULL(ls)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("log stream should not be null", K(ret), KP(ls), K(ls_id));
      } else if (OB_ISNULL(log_handler = ls->get_log_handler())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("log handler should not be null", K(ret), KP(log_handler), K(ls_id));
      } else if (OB_FAIL(log_handler->get_end_lsn(result.source_end_lsn_))) {
        LOG_WARN("failed to get source end lsn", K(ret), K(ls_id), K(arg));
      } else if (OB_FAIL(log_handler->get_end_scn(result.source_end_scn_))) {
        LOG_WARN("failed to get source end scn", K(ret), K(ls_id), K(arg));
      } else if (OB_FAIL(log_handler->locate_by_scn_coarsely(arg.replay_start_scn_, scan_lsn))) {
        if (OB_ERR_OUT_OF_LOWER_BOUND == ret) {
          if (OB_FAIL(log_handler->get_begin_lsn(scan_lsn))) {
            if (OB_ENTRY_NOT_EXIST == ret) {
              need_scan = false;
              ret = OB_SUCCESS;
            } else {
              LOG_WARN("failed to get source begin lsn", K(ret), K(ls_id), K(arg));
            }
          }
        } else if (OB_ENTRY_NOT_EXIST == ret) {
          need_scan = false;
          ret = OB_SUCCESS;
        } else {
          LOG_WARN("failed to locate source replay start scn", K(ret), K(ls_id), K(arg));
        }
      }

      if (OB_SUCC(ret) && need_scan) {
        if (!scan_lsn.is_valid()) {
          scan_lsn = result.source_end_lsn_;
        }
        if (OB_FAIL(log_handler->get_palf_base_info(scan_lsn, scan_base_info))) {
          LOG_WARN("failed to get source scan base info", K(ret), K(ls_id), K(scan_lsn), K(arg));
        } else {
          result.palf_base_info_ = scan_base_info;
          curr_base_lsn = scan_base_info.curr_lsn_;
        }
      }

      if (OB_SUCC(ret) && need_scan) {
        palf::PalfGroupBufferIterator iterator;
        if (OB_FAIL(log_handler->seek(curr_base_lsn, iterator))) {
          LOG_WARN("failed to seek source log iterator", K(ret), K(ls_id), K(curr_base_lsn), K(arg));
        } else if (OB_FAIL(iterator.set_io_context(palf::LogIOContext(palf::LogIOUser::FETCHLOG)))) {
          LOG_WARN("failed to set source iterator io context", K(ret), K(ls_id), K(arg));
        } else if (FALSE_IT(iterator.set_need_print_error(false))) {
        } else {
          palf::LogGroupEntry entry;
          palf::LSN curr_lsn;
          while (OB_SUCC(ret) && OB_SUCC(iterator.next())) {
            if (OB_FAIL(iterator.get_entry(entry, curr_lsn))) {
              LOG_WARN("failed to get source log entry", K(ret), K(ls_id), K(iterator), K(arg));
            } else if (entry.get_scn() >= arg.replay_start_scn_) {
              result.located_log_ = true;
              LOG_INFO("located standby replay start log",
                  K(ls_id), K(arg), K(curr_lsn), K(entry), K(result.palf_base_info_));
              break;
            }
          }
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
          }
        }
      }

      if (OB_SUCC(ret)
          && arg.replay_start_scn_.is_valid()
          && result.source_end_scn_.is_valid()
          && !(result.source_end_scn_ < arg.replay_start_scn_)
          && !result.located_log_) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("source end scn covers standby replay start scn, but replay start log is not located",
            K(ret), K(ls_id), K(arg), K(result));
      }

      if (OB_SUCC(ret) && !result.palf_base_info_.is_valid()) {
        if (OB_FAIL(log_handler->get_palf_base_info(result.source_end_lsn_, result.palf_base_info_))) {
          LOG_WARN("failed to get source end palf base info",
              K(ret), K(ls_id), K(result.source_end_lsn_), K(arg));
        }
      }

      if (OB_SUCC(ret) && !result.is_valid()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("built invalid standby palf base info", K(ret), K(ls_id), K(arg), K(result));
      } else {
        LOG_INFO("built standby palf base info", K(ls_id), K(arg), K(result));
      }
    }
  }
  return ret;
}

} // namespace standby
} // namespace oceanbase
