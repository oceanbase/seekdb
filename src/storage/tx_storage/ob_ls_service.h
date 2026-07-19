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

#ifndef OCEABASE_STORAGE_LS_SERVICE_
#define OCEABASE_STORAGE_LS_SERVICE_

#include "storage/tablet/ob_batch_create_tablet_arg.h"
#include "lib/allocator/ob_concurrent_fifo_allocator.h"          // ObConcurrentFIFOAllocator
#include "storage/ls/ob_ls.h"
#include "share/resource_limit_calculator/ob_resource_limit_calculator.h"
#include "storage/tx/ob_tx_result_struct.h"

namespace oceanbase
{
namespace blocksstable
{
class ObBaseStorageLogger;
class ObSuperBlockMetaEntry;
}

namespace storage
{
class ObLS;
struct DiagnoseInfo;
struct ObLSMeta;

// Maintain the tenant's single log stream.
// Support log stream meta persistent and checkpoint
class ObLSService : public ObIResourceLimitCalculatorHandler
{
	  static const int64_t DEFAULT_LOCK_TIMEOUT = 60_s;
	  static const int64_t SMALL_TENANT_MEMORY_LIMIT = 4LL * 1024 * 1024 * 1024; // 4G
public:
  int64_t break_point = -1; // just for test
public:
  ObLSService();
  virtual ~ObLSService();

  static int mtl_init(ObLSService* &ls_service);
  int init();
  int start();
  int stop();
  int wait();
  void destroy();
public:
  // for limit calculator
  virtual int get_current_info(share::ObResourceInfo &info) override;
  virtual int get_resource_constraint_value(share::ObResoureConstraintValue &constraint_value) override;
  virtual int cal_min_phy_resource_needed(share::ObMinPhyResourceResult &min_phy_res) override;
  virtual int cal_min_phy_resource_needed(const int64_t num, share::ObMinPhyResourceResult &min_phy_res) override;
public:
  // create a LS
  int create_ls();

  // create a LS for replay or update LS's meta
  // @param [in] ls_epoch, the epoch increases monotonically in tenant scope when an ls is created
  // @param [in] ls_meta, all the parameters that is needed to create a LS for replay
  int replay_create_ls(const int64_t ls_epoch, const ObLSMeta &ls_meta);
  // replay create ls commit slog.
  int replay_create_ls_commit();
  // create a LS for replay or update LS's meta
  // @param [in] ls_meta, all the parameters that is needed to create a LS for replay
  int replay_update_ls(const ObLSMeta &ls_meta);
  // Set the LS to REMOVED state for replay create abort or remove.
  int replay_remove_ls();

  // get the only log stream in seekdb.
  int get_ls(ObLS *&ls);
  int diagnose(DiagnoseInfo &info);

  // remove the ls that is creating and write abort slog.
  int gc_ls_after_replay_slog();
  // online the log stream
  int online_ls();

  // tablet operation in transactions
  // Create tablets for a ls
  // @param [in] tx_desc, trans descriptor
  // @param [in] arg, all the create parameters needed.
  // @param [out] result, the return code and trans result of the op.
  int create_tablets_in_trans(transaction::ObTxDesc &tx_desc,
                              const obcall::ObBatchCreateTabletArg &batch_arg,
                              obcall::ObCreateTabletBatchInTransRes &result);

  // remove tablets from a ls
  // @param [in] tx_desc, trans descriptor
  // @param [in] arg, all the remove parameters needed.
  // @param [out] result, the return code of the remove op.
  int remove_tablets_in_trans(transaction::ObTxDesc &tx_desc,
                              const obcall::ObBatchRemoveTabletArg &batch_arg,
                              obcall::ObRemoveTabletsInTransRes &result);

  TO_STRING_KV(K_(is_inited));
private:
  enum class ObLSCreateState {
      CREATE_STATE_INIT = 0, // begin
      CREATE_STATE_LS_ALLOCATED = 1,
      CREATE_STATE_PUBLISHED = 2,
      CREATE_STATE_WRITE_PREPARE_SLOG = 3,
      CREATE_STATE_PALF_ENABLED = 4,
      CREATE_STATE_INNER_TABLET_CREATED = 5,
      CREATE_STATE_FINISH = 6
  };

  int create_ls_();
  int inner_create_ls_(const share::SCN &create_scn, ObLS *&ls);
  int publish_ls_(ObLS *ls);
  void free_ls_(ObLS *ls);
  void remove_ls_(ObLS *ls, const bool remove_from_disk, const bool write_slog);
  int stop_and_remove_ls_(ObLS *ls, const bool remove_from_disk);
  int replay_remove_ls_();
  int replay_create_ls_(const int64_t ls_epoch, const ObLSMeta &ls_meta);
  int replay_update_ls_(const ObLSMeta &ls_meta);
  int post_create_ls_(ObLS *ls);
  void del_ls_after_create_ls_failed_(ObLSCreateState& ls_create_state, ObLS *ls);

  // for resource limit calculator
  int cal_min_phy_resource_needed_(ObMinPhyResourceResult &min_phy_res);
  int get_resource_constraint_value_(ObResoureConstraintValue &constraint_value);

private:
  bool is_inited_;
  bool is_running_; // used by create/remove, only can be used after start and before stop.
  bool is_stopped_;
  ObLS *ls_;

  common::ObConcurrentFIFOAllocator ls_allocator_;
  // protect the create and remove process
  lib::ObMutex change_lock_;

  DISALLOW_COPY_AND_ASSIGN(ObLSService);
};

}
}
namespace oceanbase
{
namespace storage
{
int get_ls_readable_scn(share::SCN &readable_scn);
}
}

#endif
