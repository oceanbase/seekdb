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

#ifndef OCEABASE_STORAGE_LS_ROLE_HANDLER_
#define OCEABASE_STORAGE_LS_ROLE_HANDLER_

#include "lib/net/ob_addr.h" // ObAddr
namespace oceanbase
{
namespace storage
{
class ObLS;

class ObLSRoleHandler
{
public:
  ObLSRoleHandler() : is_inited_(false), ls_(nullptr) {}
  ~ObLSRoleHandler() {}
  int init(ObLS *ls);
public:
  // coordinate log stream leader change
  // @param [in] leader, new leader
  int change_leader(const common::ObAddr &leader);

  // for change leader callback.
  int leader_revoke();
  int leader_takeover();
  int leader_active();

private:
  bool is_inited_;
  ObLS *ls_;
};

} // storage
} // oceanbase
#endif
