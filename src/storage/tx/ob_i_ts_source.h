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

#ifndef OCEANBASE_TRANSACTION_OB_I_TS_SOURCE_
#define OCEANBASE_TRANSACTION_OB_I_TS_SOURCE_

#include <stdint.h>
#include "lib/utility/ob_print_utils.h"
#include "ob_trans_define.h"

namespace oceanbase
{
namespace transaction
{
class ObTsCbTask;

enum
{
  TS_SOURCE_UNKNOWN = -1,
  TS_SOURCE_GTS = 0,
  MAX_TS_SOURCE,
};

OB_INLINE bool is_valid_ts_source(const int ts_type)
{
  return TS_SOURCE_UNKNOWN < ts_type && ts_type < MAX_TS_SOURCE;
}

inline bool is_ts_type_external_consistent(const int64_t ts_type)
{
  return ts_type == TS_SOURCE_GTS;
}

class ObTsParam
{
public:
  ObTsParam() { reset(); }
  ~ObTsParam() { destroy(); }
  void reset() { need_inc_ = true; }
  void destroy() { reset(); }
  void set_need_inc(const bool need_inc) { need_inc_ = need_inc; }
  bool need_inc() const { return need_inc_; }
private:
  // Whether the gts value needs to be +1, it is added by default in gts cache management
  bool need_inc_;
};

}
}//end of namespace oceanbase

#endif //OCEANBASE_TRANSACTION_OB_I_TS_SOURCE_
