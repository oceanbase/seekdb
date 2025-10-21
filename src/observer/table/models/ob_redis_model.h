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

#ifndef OCEANBASE_OBSERVER_TABLE_MODELS_OB_REDIS_MODEL_H_
#define OCEANBASE_OBSERVER_TABLE_MODELS_OB_REDIS_MODEL_H_

#include "ob_i_model.h"

namespace oceanbase
{   
namespace table
{

class ObRedisModel : public ObIModel
{
public:
  ObRedisModel() {}
  virtual ~ObRedisModel() {}
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObRedisModel);
};

} // end of namespace table
} // end of namespace oceanbase

#endif /* OCEANBASE_OBSERVER_TABLE_MODELS_OB_REDIS_MODEL_H_ */
