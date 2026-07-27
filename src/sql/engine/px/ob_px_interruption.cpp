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

#define USING_LOG_PREFIX SQL_ENG

#include "ob_px_interruption.h"
#include "sql/engine/px/ob_dfo.h"


using namespace oceanbase::common;
using namespace oceanbase::sql;

OB_SERIALIZE_MEMBER(ObPxInterruptID, query_interrupt_id_, px_interrupt_id_);

ObPxInterruptGuard::ObPxInterruptGuard(const ObInterruptibleTaskID &interrupt_id)
{
  interrupt_id_ = interrupt_id;
  interrupt_reg_ret_ = SET_INTERRUPTABLE(interrupt_id_);
}

ObPxInterruptGuard::~ObPxInterruptGuard()
{
  if (OB_SUCCESS == interrupt_reg_ret_) {
    UNSET_INTERRUPTABLE(interrupt_id_);
  }
}

int ObInterruptUtil::broadcast_px(ObIArray<ObDfo *> &dfos, int int_code)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  for (int64_t idx = 0; idx < dfos.count(); ++idx) {
    if (OB_SUCCESS != (tmp_ret = broadcast_dfo(dfos.at(idx), int_code))) {
      LOG_WARN("fail interrupt dfo", K(idx), K(ret));
    }
  }
  return ret;
}

int ObInterruptUtil::broadcast_dfo(ObDfo *dfo, int code)
{
  int ret = OB_SUCCESS;
  ObInterruptCode int_code(code,
                           GETTID(),
                           GCTX.self_addr(),
                           "PX ABORT DFO");
  ObGlobalInterruptManager *manager = ObGlobalInterruptManager::getInstance();
  if (OB_ISNULL(dfo)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("NULL ptr unexpected", K(ret));
  } else {
    ObInterruptibleTaskID interrupt_id = dfo->get_interrupt_id().px_interrupt_id_;
    if (OB_FAIL(manager->interrupt(interrupt_id, int_code))) {
      LOG_WARN("fail to interrupt local dfo workers", K(ret), K(int_code), K(interrupt_id));
    } else {
      LOG_INFO("successfully interrupted local dfo workers", K(int_code), K(interrupt_id));
    }
  }
  return ret;
}

int ObInterruptUtil::regenerate_interrupt_id(ObDfo &dfo)
{
  int ret = OB_SUCCESS;
  ObIArray<ObPxSqcMeta> &sqcs = dfo.get_sqcs();
  // Each time an interrupt is sent, the sequence number of the interrupt needs to be incremented by 1 and set in the sqc structure,
  // Avoid misinterrupting retry of the sqc
  ObDfoInterruptIdGen::inc_seqnum(dfo.get_interrupt_id().px_interrupt_id_);

  ARRAY_FOREACH_X(sqcs, j, cnt, OB_SUCC(ret)) {
    sqcs.at(j).set_interrupt_id(dfo.get_interrupt_id());
  }
  return ret;
}
// Fallback function, SQC notifies task to exit as soon as possible
int ObInterruptUtil::interrupt_tasks(ObPxSqcMeta &sqc, int code)
{
  int ret = OB_SUCCESS;
  ObInterruptCode int_code(code,
                           GETTID(),
                           GCTX.self_addr(),
                           "SQC ABORT TASK");
  ObGlobalInterruptManager *manager = ObGlobalInterruptManager::getInstance();
  ObInterruptibleTaskID interrupt_id = sqc.get_interrupt_id().px_interrupt_id_;
  if(OB_FAIL(manager->interrupt(interrupt_id, int_code))) {
    LOG_WARN("fail to interrupt local tasks", K(ret), K(int_code), K(interrupt_id));
  } else {
    LOG_INFO("success to send interrupt message to local tasks",
             K(int_code), K(interrupt_id));
  }
  return ret;
}

// SQC sends interrupt to QC
int ObInterruptUtil::interrupt_qc(ObPxSqcMeta &sqc, int code)
{
  int ret = OB_SUCCESS;
  ObInterruptCode int_code(code,
                           GETTID(),
                           GCTX.self_addr(),
                           "SQC ABORT QC");
  ObGlobalInterruptManager *manager = ObGlobalInterruptManager::getInstance();
  ObInterruptibleTaskID interrupt_id = sqc.get_interrupt_id().query_interrupt_id_;

  if (OB_ISNULL(manager)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(manager->interrupt(interrupt_id, int_code))) {
    LOG_WARN("fail to interrupt local qc", K(int_code), K(ret));
  } else {
    LOG_TRACE("sqc notified local qc to interrupt",
              "qc_id", sqc.get_qc_id(),
              "interrupt_id", interrupt_id,
              "sqc_id", sqc.get_sqc_id(),
              K(int_code));
  }
  return ret;
}
// Task send interrupt to QC
int ObInterruptUtil::interrupt_qc(ObPxTask &task, int code)
{
  int ret = OB_SUCCESS;
  ObInterruptCode int_code(code,
                           GETTID(),
                           GCTX.self_addr(),
                           "TASK ABORT QC");
  ObGlobalInterruptManager *manager = ObGlobalInterruptManager::getInstance();
  ObInterruptibleTaskID interrupt_id = task.get_interrupt_id().query_interrupt_id_;

  if (OB_ISNULL(manager)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(manager->interrupt(interrupt_id, int_code))) {
    LOG_WARN("fail to interrupt local qc", K(int_code), K(ret));
  } else {
    LOG_TRACE("task notified local qc to interrupt",
              "qc_id", task.get_qc_id(),
              "task_id", task.get_task_id(),
              "task_co_id", task.get_task_co_id(),
              "interrupt_id", interrupt_id,
              K(int_code));
  }
  return ret;
}

int ObInterruptUtil::generate_query_interrupt_id(const uint64_t px_sequence_id,
                                                 ObInterruptibleTaskID &interrupt_id)
{
  int ret = OB_SUCCESS;
  uint64_t timestamp = ObTimeUtility::current_time();
  // Take the low 12 bits
  timestamp = (uint64_t)0xfff & timestamp;
  interrupt_id.first_ = px_sequence_id;
  interrupt_id.last_ = timestamp;
  return ret;
}

int ObInterruptUtil::generate_px_interrupt_id(const uint32_t qc_id,
                                              const uint64_t px_sequence_id,
                                              const int64_t dfo_id,
                                              ObInterruptibleTaskID &interrupt_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(qc_id <= 0 ||
                  dfo_id < 0 ||
                  dfo_id > ObDfo::MAX_DFO_ID)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("QC id is less than or equal 0 in generate px interrupt id", K(qc_id), K(dfo_id));
  } else {
    uint64_t timestamp = ObTimeUtility::current_time();
    // Take the low 12 bits
    timestamp = (uint64_t)0xfff & timestamp;
    interrupt_id.first_ = px_sequence_id;
    // [ qc_id (10bits)][ dfo_id (10bits) ][ timestamp (12bits) ]
    interrupt_id.last_ = (uint64_t)qc_id << 22 |
        (uint64_t)dfo_id << 12 | (uint64_t)timestamp;
  }
  return ret;
}

void ObDfoInterruptIdGen::inc_seqnum(common::ObInterruptibleTaskID &px_interrupt_id)
{
  // Patch the seq value (low 12 bits of last_) to the lowest 12 bits, increment by 1 each call
  uint64_t last = px_interrupt_id.last_;
  px_interrupt_id.last_ = (last & (0xffffffff << 12)) | (((last & 0xfff) + 1) & 0xfff);
}
