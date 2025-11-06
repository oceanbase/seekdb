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

#ifndef OCEABASE_STORAGE_OB_LS_SAFE_DESTROY_TASK_
#define OCEABASE_STORAGE_OB_LS_SAFE_DESTROY_TASK_

#include "storage/tx_storage/ob_ls_map.h"
#include "storage/tx_storage/ob_ls_handle.h" //ObLSHandle
#include "storage/tx_storage/ob_safe_destroy_handler.h"

namespace oceanbase
{
namespace storage
{
class ObLSSafeDestroyTask : public ObSafeDestroyTask
{
public:
  ObLSSafeDestroyTask();
  ~ObLSSafeDestroyTask();
  virtual bool safe_to_destroy() override;
  virtual void destroy() override;
  int get_ls_id(share::ObLSID &ls_id) const;
  INHERIT_TO_STRING_KV("ObSafeDestroyTask", ObSafeDestroyTask, K_(is_inited),
                       K_(tenant_id), K_(ls_handle), KP_(ls_service));
private:
  bool is_inited_;
  // used to switch to the tenant to make sure destroy process is right.
  uint64_t tenant_id_;
  // contain the ls need to check.
  ObLSHandle ls_handle_;
  // the ls service, if a ls is not safe to destroy the ls service
  // should not safe to destroy too.
  ObLSService *ls_service_;
};

class ObSafeDestroyCheckLSExist
{
public:
  explicit ObSafeDestroyCheckLSExist(const share::ObLSID &ls_id)
    : err_code_(common::OB_SUCCESS),
      exist_(false),
      ls_id_(ls_id)
  {}
  int is_exist() const { return exist_; }
  int get_ret_code() const { return err_code_; }
private:
  int err_code_;
  bool exist_;
  share::ObLSID ls_id_;
};

} // storage
} // oceanbase
#endif
