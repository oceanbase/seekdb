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

#define USING_LOG_PREFIX SQL_PC
#include "ob_ps_cache_callback.h"


namespace oceanbase
{
using namespace common;
namespace sql
{

void ObPsStmtItemRefAtomicOp::operator()(const PsStmtIdKV &entry)
{
  if (NULL != entry.second) {
    if (entry.second->check_erase_inc_ref_count()) {//has been marked by other threads
      callback_ret_ = OB_EAGAIN;
      LOG_INFO("element will be free, try again", K(entry), K(callback_ret_));
    } else {//When execution reaches this code block, the reference count will not be 0, because operator() is protected by the lock in the hashtable}
      callback_ret_ = OB_SUCCESS;
      stmt_item_ = entry.second;
    }
  } else {
    callback_ret_ = OB_ERR_UNEXPECTED;
    LOG_WARN_RET(callback_ret_, "value is NULL", K(entry), K(callback_ret_));
  }
}

int ObPsStmtItemRefAtomicOp::get_value(ObPsStmtItem *&ps_item)
{
  int ret = OB_SUCCESS;
  ps_item = NULL;
  if (OB_ISNULL(stmt_item_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("stmt_item should not be null", K(ret));
  } else {
    ps_item = stmt_item_;
  }
  return ret;
}


void ObPsStmtItemEraseAtomicOp::operator()(const PsStmtIdKV &entry)
{
  if (OB_ISNULL(entry.second)) {
    ret_ = OB_HASH_NOT_EXIST;
    LOG_WARN_RET(ret_, "entry not exist", K_(ret));
  } else if (entry.second->get_ps_stmt_id() == stmt_id_) {
    if (ATOMIC_BCAS(entry.second->get_is_expired_evicted_ptr(), false, true)) {
      need_erase_ = true;
    }
  }
}

void ObPsStmtInfoRefAtomicOp::operator ()(const PsStmtInfoKV &entry)
{
  if (NULL != entry.second) {
    if (entry.second->check_erase_inc_ref_count()) {//has been marked by other threads
      callback_ret_ = OB_EAGAIN;
      LOG_INFO("element will be free, try again", K(entry), K(callback_ret_));
    } else {//When execution reaches this code block, the reference count will not be 0, because operator() is protected by the lock in the hashtable}
      callback_ret_ = OB_SUCCESS;
      stmt_info_ = entry.second;
    }
  } else {
    callback_ret_ = OB_ERR_UNEXPECTED;
    LOG_WARN_RET(callback_ret_, "value is NULL", K(entry), K(callback_ret_));
  }
}

int ObPsStmtInfoRefAtomicOp::get_value(ObPsStmtInfo *&ps_info)
{
  int ret = OB_SUCCESS;
  ps_info = NULL;
  if (OB_ISNULL(stmt_info_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("stmt info should not be null", K(ret));
  } else {
    ps_info = stmt_info_;
  }
  return ret;
}

void ObPsStmtInfoDerefAtomicOp::operator()(const PsStmtInfoKV &entry)
{
  if (OB_ISNULL(entry.second)) {
    ret_ = OB_HASH_NOT_EXIST;
    LOG_WARN_RET(ret_, "entry not exist", K_(ret));
  } else {
    entry.second->dec_ref_count();
  }
}

void ObPsStmtInfoDestroyAtomicOp::operator()(const PsStmtInfoKV &entry)
{
  if (OB_ISNULL(entry.second)) {
    ret_ = OB_ERR_UNEXPECTED;
    LOG_WARN_RET(ret_, "ps stmt info is NULL", K_(ret));
  } else {
    marked_erase_ = entry.second->try_erase();
  }
}


//get pcvs and lock
int ObPsPCVSetAtomicOp::get_value(ObPCVSet *&pcvs)
{
  int ret = OB_SUCCESS;
  pcvs = NULL;
  if (OB_ISNULL(pcv_set_)) {
    ret = OB_NOT_INIT;
    SQL_PC_LOG(WARN, "invalid argument", K(pcv_set_));
  } else if (OB_SUCC(lock(*pcv_set_))) {
    pcvs = pcv_set_;
  } else {
    if (NULL != pcv_set_) {
      pcv_set_->dec_ref_count(ref_handle_);
    }
    SQL_PC_LOG(ERROR, "failed to get read lock of plan cache value", K(ret));
  }
  return ret;
}
}
}
