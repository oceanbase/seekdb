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

#pragma once

#include "lib/allocator/ob_allocator.h"
#include "lib/net/ob_addr.h"

namespace oceanbase
{
namespace observer
{

class ObTableDirectLoadExecContext
{
public:
  explicit ObTableDirectLoadExecContext(common::ObIAllocator &allocator)
    : allocator_(allocator), tenant_id_(0), user_id_(0), database_id_(0)
  {
  }
  ~ObTableDirectLoadExecContext() = default;
  common::ObIAllocator &get_allocator() { return allocator_; }
  void set_tenant_id(uint64_t tenant_id) { tenant_id_ = tenant_id; }
  uint64_t get_tenant_id() const { return tenant_id_; }
  void set_user_id(uint64_t user_id) { user_id_ = user_id; }
  uint64_t get_user_id() const { return user_id_; }
  void set_database_id(uint64_t database_id) { database_id_ = database_id; }
  uint64_t get_database_id() const { return database_id_; }
  void set_user_client_addr(const ObAddr &user_client_addr)
  {
    user_client_addr_ = user_client_addr;
  }
  const ObAddr get_user_client_addr() const { return user_client_addr_; }
private:
  DISALLOW_COPY_AND_ASSIGN(ObTableDirectLoadExecContext);
private:
  common::ObIAllocator &allocator_;
  uint64_t tenant_id_;
  uint64_t user_id_;
  uint64_t database_id_;
  ObAddr user_client_addr_;
};

} // namespace observer
} // namespace oceanbase
