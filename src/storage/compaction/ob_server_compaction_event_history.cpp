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

#define USING_LOG_PREFIX STORAGE
#include "storage/compaction/ob_server_compaction_event_history.h"
#include "share/rc/ob_server_runtime.h"

namespace oceanbase
{
namespace compaction
{

const static char *ObCompactionEventStr[] = {
    "RECEIVE_BROADCAST_SCN",
    "GET_FREEZE_INFO",
    "WEAK_READ_TS_READY",
    "SCHEDULER_LOOP",
    "TABLET_COMPACTION_FINISHED",
    "COMPACTION_FINISH_CHECK",
    "COMPACTION_REPORT",
    "RS_REPAPRE_UNFINISH_TABLE_IDS",
    "RS_FINISH_CUR_LOOP",
    "LS_STATE_CHANGED",
    "CHOOSE_NEW_EXEC_SVR"
};

const char *ObServerCompactionEvent::get_comp_event_str(enum ObCompactionEvent event)
{
  STATIC_ASSERT(static_cast<int64_t>(COMPACTION_EVENT_MAX) == ARRAYSIZEOF(ObCompactionEventStr), "compaction event str len is mismatch");
  const char *str = "";
  if (event >= COMPACTION_EVENT_MAX || event < RECEIVE_BROADCAST_SCN) {
    str = "invalid_type";
  } else {
    str = ObCompactionEventStr[event];
  }
  return str;
}

const static char *ObCompactionRoleStr[] = {
    "ROOT_SERVICE",
    "STORAGE",
    "LS_LEADER",
    "LS_SVR"
};

const char *ObServerCompactionEvent::get_comp_role_str(enum ObCompactionRole role)
{
  STATIC_ASSERT(static_cast<int64_t>(COMPACTION_ROLE_MAX) == ARRAYSIZEOF(ObCompactionRoleStr), "compaction role str len is mismatch");
  const char *str = "";
  if (role >= COMPACTION_ROLE_MAX || role < ROOT_SERVICE) {
    str = "invalid_role";
  } else {
    str = ObCompactionRoleStr[role];
  }
  return str;
}

int ObServerCompactionEvent::generate_event_str(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  if (0 == strlen(comment_)) {
    if (OB_FAIL(databuff_printf(buf, buf_len, "%s", get_comp_event_str(event_)))) {
    }
  } else if (OB_FAIL(databuff_printf(buf, buf_len, "%s:%s", get_comp_event_str(event_), comment_))) {
  }
  return ret;
}

int ObServerCompactionEventHistory::server_module_init(ObServerCompactionEventHistory* &event_history)
{
  return event_history->init();
}

int ObServerCompactionEventHistory::init()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObInfoRingArray::init(SERVER_EVENT_MAX_CNT))) {
  }
  return ret;
}

void ObServerCompactionEventHistory::destroy()
{
  ObInfoRingArray::destroy();
}

int ObServerCompactionEventHistory::add_event(const ObServerCompactionEvent &event)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!event.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(event));
  } else if (OB_FAIL(ObInfoRingArray::add(event))) {
  }
  return ret;
}

int ObServerCompactionEventHistory::get_last_event(ObServerCompactionEvent &event)
{
  int ret = OB_SUCCESS;
  if (size() > 0) {
    if (OB_FAIL(get(get_last_pos(), event))) {
    }
  } else {
    event.reset();
  }
  return ret;
}

/*
 * ObServerCompactionEventIterator implement
 * */

int ObServerCompactionEventIterator::open()
{
  int ret = OB_SUCCESS;
  if (is_opened_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("The ObServerCompactionEventIterator has been opened", K(ret));
  }
  if (OB_SUCC(ret)) {
    {
      SERVER_MODULE_SCOPE {
        if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::compaction::ObServerCompactionEventHistory>()->get_list(event_array_))) {
        }
      } else {
        if (OB_SERVER_RUNTIME_NOT_READY != ret) {
          STORAGE_LOG(WARN, "enter server module scope failed", K(ret));
        } else {
          ret = OB_SUCCESS;
          continue;
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    cur_idx_ = 0;
    is_opened_ = true;
  }
  return ret;
}

void ObServerCompactionEventIterator::reset()
{
  event_array_.reset();
  cur_idx_ = 0;
  is_opened_ = false;
}

int ObServerCompactionEventIterator::get_next_info(ObServerCompactionEvent &info)
{
  int ret = OB_SUCCESS;
  if (!is_opened_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (cur_idx_ >= event_array_.count()) {
    ret = OB_ITER_END;
  } else {
    info = event_array_.at(cur_idx_);
    ++cur_idx_;
  }
  return ret;
}

} // namespace compaction
} // namespace oceanbase
