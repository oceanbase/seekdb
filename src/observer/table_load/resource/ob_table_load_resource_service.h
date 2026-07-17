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

#include "lib/lock/ob_spin_lock.h"
#include "observer/table_load/ob_table_load_struct.h"
#include "observer/table_load/resource/ob_table_load_resource_manager.h"

namespace oceanbase
{
namespace observer
{
class ObTableLoadResourceManager;

class ObTableLoadResourceService
{
public:	
	ObTableLoadResourceService() 
    : resource_manager_(nullptr),
      is_inited_(false)
  {
  }
  virtual ~ObTableLoadResourceService();
	int init();
  static int mtl_init(ObTableLoadResourceService *&service);
	int start() { return common::OB_SUCCESS; };
  void stop();
  void wait();
  void destroy();

  static int apply_resource(ObDirectLoadResourceApplyArg &arg);
  static int release_resource(ObDirectLoadResourceReleaseArg &arg);
	
private:
	int alloc_resource_manager();
	int delete_resource_manager();
	int check_inner_stat();
private:
  mutable obsys::ObRWLock rw_lock_;
	ObTableLoadResourceManager *resource_manager_;
	bool is_inited_;
};

} // namespace observer
} // namespace oceanbase
