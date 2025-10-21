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

#ifndef OCEANBASE_OBSERVER_TABLE_MODELS_OB_MODEL_FACTORY_H_
#define OCEANBASE_OBSERVER_TABLE_MODELS_OB_MODEL_FACTORY_H_

#include "ob_table_model.h"
#include "ob_hbase_model.h"
#include "ob_redis_model.h"

namespace oceanbase
{   
namespace table
{

class ObModelFactory
{
public:
  static int get_model_guard(common::ObIAllocator &alloc, const ObTableEntityType &type, ObModelGuard &guard)
  {
    int ret = OB_SUCCESS;
    ObIModel *tmp_model = nullptr;
    char *buf = nullptr;

    switch (type) {
      case ObTableEntityType::ET_KV: {
        if (OB_ISNULL(buf = static_cast<char *>(alloc.alloc(sizeof(ObTableModel))))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          COMMON_LOG(WARN, "fail to alloc ObTableModel", K(ret), K(sizeof(ObTableModel)));
        } else {
          tmp_model = new (buf) ObTableModel();
        }
        break;
      }
      case ObTableEntityType::ET_HKV: {
        if (OB_ISNULL(buf = static_cast<char *>(alloc.alloc(sizeof(ObHBaseModel))))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          COMMON_LOG(WARN, "fail to alloc ObHBaseModel", K(ret), K(sizeof(ObHBaseModel)));
        } else {
          tmp_model = new (buf) ObHBaseModel();
        }
        break;
      }
      case ObTableEntityType::ET_REDIS: {
        if (OB_ISNULL(buf = static_cast<char *>(alloc.alloc(sizeof(ObRedisModel))))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          COMMON_LOG(WARN, "fail to alloc ObRedisModel", K(ret), K(sizeof(ObRedisModel)));
        } else {
          tmp_model = new (buf) ObRedisModel();
        }
        break;
      }
      default: {
        ret = OB_ERR_UNEXPECTED;
        COMMON_LOG(WARN, "invalid entity type", K(ret), K(type));
        break;
      }
    }

    if (OB_SUCC(ret)) {
      guard.set_allocator(&alloc);
      guard.set_model(tmp_model);
    }

    return ret;
  }
};

} // end of namespace table
} // end of namespace oceanbase

#endif /* OCEANBASE_OBSERVER_TABLE_MODELS_OB_MODEL_FACTORY_H_ */
