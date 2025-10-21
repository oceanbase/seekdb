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

#ifndef OCEANBASE_TABLE_OB_REDIS_TTL_H_
#define OCEANBASE_TABLE_OB_REDIS_TTL_H_

#include "observer/table/redis/ob_redis_meta.h"

namespace oceanbase
{
namespace table
{
class ObRedisTTLCtx
{
public:
  explicit ObRedisTTLCtx() : meta_(nullptr), model_(ObRedisDataModel::MODEL_MAX), return_meta_(false)
  {}
  virtual ~ObRedisTTLCtx()
  {
    reset();
  }
  void reset();
  OB_INLINE const ObRedisMeta *get_meta() const
  {
    return meta_;
  }
  OB_INLINE bool is_return_meta() const
  {
    return return_meta_;
  }
  OB_INLINE void set_meta(const ObRedisMeta *meta)
  {
    meta_ = meta;
  }
  OB_INLINE void set_return_meta(bool flag)
  {
    return_meta_ = flag;
  }
  OB_INLINE ObRedisDataModel get_model() const
  {
    return model_;
  }
  OB_INLINE void set_model(ObRedisDataModel model)
  {
    model_ = model;
  }

private:
  const ObRedisMeta *meta_;
  ObRedisDataModel model_;
  bool return_meta_;  // result include redis meta (whether meta is expired or not)
};

}  // namespace table
}  // namespace oceanbase
#endif /* OCEANBASE_TABLE_OB_REDIS_TTL_H_ */
