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

#include "storage/ls/ob_ls_state.h"

namespace oceanbase
{
namespace storage
{


// the state machine of ObLSRunningState
//STATE \ ACTION    CREATE_FINISH   ONLINE    PRE_OFFLINE    POST_OFFLINE    STOP
//--------------------------------------------------------------------------------
//INIT              OFFLINED        N         N              N               STOPPED
//RUNNING           N               RUNNING   OFFLINING      N               N
//OFFLINING         N               N         OFFLINING      OFFLINED        N
//OFFLINED          N               RUNNING   OFFLINED       OFFLINED        STOPPED
//STOPPED           N               N         STOPPED        STOPPED         STOPPED
int ObLSRunningState::StateHelper::switch_state(const int64_t op)
{
  int ret = OB_SUCCESS;
  static const int64_t N = State::INVALID;
  static const int64_t LS_INIT = State::LS_INIT;
  static const int64_t LS_RUNNING = State::LS_RUNNING;
  static const int64_t LS_OFFLINING = State::LS_OFFLINING;
  static const int64_t LS_OFFLINED = State::LS_OFFLINED;
  static const int64_t LS_STOPPED = State::LS_STOPPED;

  static const int64_t STATE_MAP[State::MAX][Ops::MAX] = {
          //      CREATE_FINISH   ONLINE       PRE_OFFLINE    POST_OFFLINE       STOP
/* INIT */        {LS_OFFLINED,   N,           N,             N,                 LS_STOPPED},
/* RUNNING */     {N,             LS_RUNNING,  LS_OFFLINING,  N,                 N},
/* OFFLINING */   {N,             N,           LS_OFFLINING,  LS_OFFLINED,       N},
/* OFFLINED */    {N,             LS_RUNNING,  LS_OFFLINED,   LS_OFFLINED,       LS_STOPPED},
/* STOPPED */     {N,             N,           LS_STOPPED,    LS_STOPPED,        LS_STOPPED},
  };

  if (OB_UNLIKELY(!Ops::is_valid(op))) {
    LOG_WARN("invalid argument", K(op));
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(!State::is_valid(state_))) {
    LOG_WARN("ObLSRunningState current state is invalid", K_(state), K(op));
    ret = OB_ERR_UNEXPECTED;
  } else {
    const int64_t new_state = STATE_MAP[state_][op];
    if (OB_UNLIKELY(!State::is_valid(new_state))) {
      ret = OB_STATE_NOT_MATCH;
    } else {
      last_state_ = state_;
      state_ = new_state;
    }
  }
  if (OB_SUCC(ret)) {
    _LOG_INFO("ObLSRunningState switch state success(%s ~> %s, op=%s)",
              State::state_str(last_state_), State::state_str(state_), Ops::op_str(op));
  } else {
    _LOG_ERROR("ObLSRunningState switch state error(ret=%d, state=%s, op=%s)",
               ret, State::state_str(state_), Ops::op_str(op));
  }
  return ret;
}

int ObLSRunningState::create_finish()
{
  int ret = OB_SUCCESS;
  StateHelper state_helper(state_);
  if (OB_FAIL(state_helper.switch_state(Ops::CREATE_FINISH))) {
  }
  return ret;
}

int ObLSRunningState::online()
{
  int ret = OB_SUCCESS;
  StateHelper state_helper(state_);
  if (OB_FAIL(state_helper.switch_state(Ops::ONLINE))) {
  }
  return ret;
}

int ObLSRunningState::pre_offline()
{
  int ret = OB_SUCCESS;
  StateHelper state_helper(state_);
  if (OB_FAIL(state_helper.switch_state(Ops::PRE_OFFLINE))) {
  }
  return ret;
}

int ObLSRunningState::post_offline()
{
  int ret = OB_SUCCESS;
  StateHelper state_helper(state_);
  if (OB_FAIL(state_helper.switch_state(Ops::POST_OFFLINE))) {
  }
  return ret;
}

int ObLSRunningState::stop()
{
  int ret = OB_SUCCESS;
  StateHelper state_helper(state_);
  if (OB_FAIL(state_helper.switch_state(Ops::STOP))) {
  }
  return ret;
}

ObLSPersistentState &ObLSPersistentState::operator=(const ObLSPersistentState &other)
{
  if (this != &other) {
    state_ = other.state_;
  }
  return *this;
}

ObLSPersistentState &ObLSPersistentState::operator=(const int64_t state)
{
  state_ = state;
  return *this;
}

// the state machine of ObLSPersistentState
//STATE \ ACTION    START_WORK   START_RESTORE FINISH_RESTORE REMOVE
//-------------------------------------------------------------------
//INIT              NORMAL       RESTORE     N              ZOMBIE
//NORMAL            NORMAL       RESTORE     N              ZOMBIE
//CREATE_ABORTED    N            N           N              N
//ZOMBIE            N            N           N              ZOMBIE
//RESTORE           N            RESTORE     NORMAL         ZOMBIE
int ObLSPersistentState::StateHelper::switch_state(const int64_t op)
{
  int ret = OB_SUCCESS;
  static const int64_t N = State::INVALID;
  static const int64_t LS_INIT = State::LS_INIT;
  static const int64_t LS_NORMAL = State::LS_NORMAL;
  static const int64_t LS_CREATE_ABORTED = State::LS_CREATE_ABORTED;
  static const int64_t LS_ZOMBIE = State::LS_ZOMBIE;
  static const int64_t LS_RESTORE = State::LS_RESTORE;

  static const int64_t STATE_MAP[State::MAX][Ops::MAX] = {
          //         START_WORK      START_RESTORE FINISH_RESTORE REMOVE
/* INIT */           {LS_NORMAL,     LS_RESTORE,  N,             LS_ZOMBIE},
/* NORMAL */         {LS_NORMAL,     LS_RESTORE,  N,             LS_ZOMBIE},
/* CREATE_ABORTED */ {N,             N,           N,             N},
/* ZOMBIE */         {N,             N,           N,             LS_ZOMBIE},
/* RESTORE */        {N,             LS_RESTORE,  LS_NORMAL,     LS_ZOMBIE},
  };

  if (OB_UNLIKELY(!Ops::is_valid(op))) {
    LOG_WARN("invalid argument", K(op));
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(!State::is_valid(state_))) {
    LOG_WARN("ObLSPersistentState current state is invalid", K_(state), K(op));
    ret = OB_ERR_UNEXPECTED;
  } else {
    const int64_t new_state = STATE_MAP[state_][op];
    if (OB_UNLIKELY(!State::is_valid(new_state))) {
      ret = OB_STATE_NOT_MATCH;
    } else {
      last_state_ = state_;
      state_ = new_state;
    }
  }
  if (OB_SUCC(ret)) {
    _LOG_INFO("ObLSPersistentState switch state success(%s ~> %s, op=%s)",
              State::state_str(last_state_), State::state_str(state_), Ops::op_str(op));
  } else {
    _LOG_ERROR("ObLSPersistentState switch state error(ret=%d, state=%s, op=%s)",
               ret, State::state_str(state_), Ops::op_str(op));
  }
  return ret;
}

int ObLSPersistentState::start_work()
{
  int ret = OB_SUCCESS;
  StateHelper state_helper(state_);
  if (OB_FAIL(state_helper.switch_state(Ops::START_WORK))) {
  }
  return ret;
}

int ObLSPersistentState::start_restore()
{
  int ret = OB_SUCCESS;
  StateHelper state_helper(state_);
  if (OB_FAIL(state_helper.switch_state(Ops::START_RESTORE))) {
  }
  return ret;
}

int ObLSPersistentState::finish_restore()
{
  int ret = OB_SUCCESS;
  StateHelper state_helper(state_);
  if (OB_FAIL(state_helper.switch_state(Ops::FINISH_RESTORE))) {
  }
  return ret;
}

int ObLSPersistentState::remove()
{
  int ret = OB_SUCCESS;
  StateHelper state_helper(state_);
  if (OB_FAIL(state_helper.switch_state(Ops::REMOVE))) {
  }
  return ret;
}

int ObLSPersistentState::serialize(char* buf, const int64_t buf_len, int64_t& pos) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf) || OB_UNLIKELY(buf_len <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), KP(buf), K(buf_len));
  } else if (OB_FAIL(serialization::encode_vi64(buf, buf_len, pos, state_))) {
  }
  return ret;
}

int ObLSPersistentState::deserialize(const char* buf, const int64_t data_len, int64_t& pos)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf) || OB_UNLIKELY(data_len <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), KP(buf), K(data_len));
  } else if (OB_FAIL(serialization::decode_vi64(buf, data_len, pos, &state_))) {
  }
  return ret;
}

int64_t ObLSPersistentState::get_serialize_size() const
{
  int64_t size = 0;
  size += serialization::encoded_length_vi64(state_);
  return size;
}

}
}
