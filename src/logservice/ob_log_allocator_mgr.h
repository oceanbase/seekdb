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

#ifndef _OB_LOG_ALLOCATOR_MGR_H_
#define _OB_LOG_ALLOCATOR_MGR_H_

#include "share/ob_define.h"
#include "share/resource/ob_server_runtime_config.h"

namespace oceanbase
{
namespace common
{
class ObILogAllocator;
class ObLogAllocator;

class ObLogAllocatorMgr
{
public:
  typedef ObLogAllocator Allocator;
  ObLogAllocatorMgr()
    : is_inited_(false), lock_(), allocator_(NULL)
  {}
  ~ObLogAllocatorMgr()
  {}
  int init();

  int get_log_allocator(ObILogAllocator *&out_allocator);
  int delete_log_allocator();
  int update_memory_limit(const share::ObServerRuntimeConfig &runtime_config);
public:
  static ObLogAllocatorMgr &get_instance();
private:
  int get_allocator_(Allocator *&out_allocator);
  int get_memstore_limit_percent_(int64_t &limit_percent) const;
  int delete_allocator_();
  int construct_allocator_(Allocator *&out_allocator);
  int create_allocator_(Allocator *&out_allocator);
private:
  bool is_inited_;
  obsys::ObRWLock lock_;
  ObLogAllocator *allocator_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObLogAllocatorMgr);
}; // end of class ObLogAllocatorMgr

#define LOG_ALLOCATOR_MGR_INSTANCE (::oceanbase::common::ObLogAllocatorMgr::get_instance())

} // end of namespace common
} // end of namespace oceanbase
#endif /* _OB_LOG_ALLOCATOR_MGR_H_ */
