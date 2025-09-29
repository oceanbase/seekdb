/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#ifndef OB_PX_INTERRUPTION_H_
#define OB_PX_INTERRUPTION_H_

#include "share/interrupt/ob_global_interrupt_call.h"
#include "lib/net/ob_addr.h"
#include "lib/container/ob_iarray.h"

namespace oceanbase
{
namespace sql
{

class ObDfo;
class ObPxTask;
class ObPxSqcMeta;
// In px, since two interrupt IDs need to be recorded, all interrupt IDs in px come from this structure
struct ObPxInterruptID
{
  OB_UNIS_VERSION(1);
public:
  ObPxInterruptID():query_interrupt_id_(0), px_interrupt_id_(0) {}
  void operator=(const ObPxInterruptID &other)
  {
    query_interrupt_id_ = other.query_interrupt_id_;
    px_interrupt_id_ = other.px_interrupt_id_;
  }
  bool operator==(const ObPxInterruptID &other) const
  {
    return query_interrupt_id_ == other.query_interrupt_id_ &&
           px_interrupt_id_ == other.px_interrupt_id_;
  }
  TO_STRING_KV(K_(query_interrupt_id), K_(px_interrupt_id));
  // Used for sqc and task to send interrupt id to qc, query also uses this id when registering interrupt
  common::ObInterruptibleTaskID query_interrupt_id_;    
  // Used for qc to send interrupt id to its sqc and tasks, sqc and tasks also use this id when registering interrupts
  common::ObInterruptibleTaskID px_interrupt_id_;  
};

class ObPxInterruptGuard
{
public:
  ObPxInterruptGuard(const common::ObInterruptibleTaskID &interrupt_id_);
  ~ObPxInterruptGuard();
  int get_interrupt_reg_ret() { return interrupt_reg_ret_; }
private:
  common::ObInterruptibleTaskID interrupt_id_;
  int interrupt_reg_ret_;
};


class ObInterruptUtil
{
public:
  // QC sends interrupt to all SQCs and Tasks under px
  static int broadcast_px(common::ObIArray<sql::ObDfo *> &dfos, int code);
  // QC sends interrupt to all SQCs and Tasks under dfo
  static int broadcast_dfo(ObDfo *dfo, int code);
  // SQC sends an interrupt to all tasks to urge them to exit as soon as possible. Handles the scenario where a task misses the QC interrupt
  static int interrupt_tasks(ObPxSqcMeta &sqc, int code);
  // DFO retry, need to use a new interrupt number to avoid being misinterrupted by interrupt residue
  static int regenerate_interrupt_id(ObDfo &dfo);
  // Only in normal px worker thread executing process call this function will set
  // px_worker_execute_start_schema_version
  static void update_schema_error_code(ObExecContext *exec_ctx, int &code,
                           int64_t px_worker_execute_start_schema_version = OB_INVALID_VERSION);
  // SQC and Tasks send interrupt to QC
  static int interrupt_qc(ObPxSqcMeta &sqc, int code, ObExecContext *exec_ctx);
  static int interrupt_qc(ObPxTask &task, int code, ObExecContext *exec_ctx);
  // Combine server_id, execution_id, and qc_id to form the interrupt id
  // Suggest using GCTX.get_server_index() instead of GCTX.get_server_id(),
  // as it guarantees uniqueness within the cluster and is constrained to a maximum value of MAX_SERVER_COUNT.
  static int generate_query_interrupt_id(const uint32_t server_index,
                                         const uint64_t px_sequence_id,
                                         common::ObInterruptibleTaskID &interrupt_id);
  // Suggest using GCTX.get_server_index() instead of GCTX.get_server_id(),
  // as it guarantees uniqueness within the cluster and is constrained to a maximum value of MAX_SERVER_COUNT.
  static int generate_px_interrupt_id(const uint32_t server_index,
                                      const uint32_t qc_id,
                                      const uint64_t px_sequence_id,
                                      const int64_t dfo_id,
                                      common::ObInterruptibleTaskID &interrupt_id);
};

class ObDfoInterruptIdGen
{
public:
  ObDfoInterruptIdGen(const common::ObInterruptibleTaskID &query_interrupt_id,
                      const uint32_t server_id,
                      const uint32_t qc_id,
                      const uint64_t px_sequence_id)
      : query_interrupt_id_(query_interrupt_id),
        server_id_(server_id),
        qc_id_(qc_id),
        px_sequence_id_(px_sequence_id)
  {}
  ~ObDfoInterruptIdGen() = default;
  int gen_id(int64_t dfo_id, ObPxInterruptID &int_id) const
  {
    int_id.query_interrupt_id_ = query_interrupt_id_;
    return ObInterruptUtil::generate_px_interrupt_id(server_id_,
                                                     qc_id_,
                                                     px_sequence_id_,
                                                     dfo_id,
                                                     int_id.px_interrupt_id_);
  }
  static void inc_seqnum(common::ObInterruptibleTaskID &px_interrupt_id);
  uint64_t get_px_sequence_id() const { return px_sequence_id_; }
private:
  const common::ObInterruptibleTaskID &query_interrupt_id_;
  const uint32_t server_id_;
  const uint32_t qc_id_;
  const uint64_t px_sequence_id_;
};

}
}

#endif // OB_PX_INTERRUPTION_H_
