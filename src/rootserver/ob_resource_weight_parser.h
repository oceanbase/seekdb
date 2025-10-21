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

#ifndef __OCENABASE_RS_RESOURCE_WEIGHT_PARSER_H__
#define __OCENABASE_RS_RESOURCE_WEIGHT_PARSER_H__

#include "share/ob_kv_parser.h"
#include "rootserver/ob_balance_info.h"

namespace oceanbase
{
namespace rootserver
{
class ObResourceWeightParser
{
public:
  /*
   * if weight doesn't not sum to 1, return OB_INVALID_CONFIG
   */
private:
  class MyCb : public share::ObKVMatchCb
  {
  public:
    MyCb(ObResourceWeight &weight) : weight_(weight) {};
    int match(const char *key, const char *value);
  private:
    /* functions */
    typedef void (*WeightSetter)(ObResourceWeight &weight, double);
    static void set_iops(ObResourceWeight &weight, double val) { weight.iops_weight_ = val; }
    static void set_cpu(ObResourceWeight &weight, double val) { weight.cpu_weight_ = val; }
    static void set_memory(ObResourceWeight &weight, double val) { weight.memory_weight_ = val; }
    static void set_disk(ObResourceWeight &weight, double val) { weight.disk_weight_ = val; }
    ObResourceWeight &weight_;
  };
private:
  /* variables */
  DISALLOW_COPY_AND_ASSIGN(ObResourceWeightParser);
};
}
}
#endif /* __OCENABASE_RS_RESOURCE_WEIGHT_PARSER_H__ */
//// end of header file


