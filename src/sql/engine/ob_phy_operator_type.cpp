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

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/ob_operator_reg.h"
#include "sql/engine/ob_phy_operator_type.h"

enum
{
  PHY_OP_XMACRO_ENTRY_COUNT = 0
#define PHY_OP_DEF(type) + 1
#include "sql/engine/ob_phy_operator_type.h"
#undef PHY_OP_DEF
};
static_assert(PHY_OP_XMACRO_ENTRY_COUNT == oceanbase::sql::PHY_END + 1,
              "physical operator X-macro header must support repeated inclusion");

using namespace oceanbase::sql;
using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{

const char *get_phy_op_name(ObPhyOperatorType type)
{
  const char *ret_char = NULL;
  static const char *ObPhyOpName[PHY_END + 2] = {
#define PHY_OP_DEF(type) #type,
#include "ob_phy_operator_type.h"
#undef PHY_OP_DEF
#define END ""
    END
#undef END
  };

  if (type >= 0 && type < PHY_END + 2)
  {
    ret_char = ObPhyOpName[type];
  } else {
    ret_char = "INVALID_OP";
  }
  return ret_char;
}

ObPhyOperatorTypeDescSet::ObPhyOperatorTypeDescSet()
{
#define PHY_OP_DEF(type) set_type_str(type, #type);
#include "sql/engine/ob_phy_operator_type.h"
#undef PHY_OP_DEF
}

void ObPhyOperatorTypeDescSet::set_type_str(ObPhyOperatorType type, const char *type_str)
{
  if (OB_LIKELY(type >= PHY_INVALID && type < PHY_END)) {
    set_[type].name_ = type_str;
  } else {
    LOG_WARN_RET(OB_ERR_UNEXPECTED, "invalid phy operator", K(type));
  }
}

const char *ObPhyOperatorTypeDescSet::get_type_str(ObPhyOperatorType type) const
{
  const char *ret = "UNKNOWN_PHY_OP";
  if (OB_LIKELY(type >= PHY_INVALID && type < PHY_END)) {
    ret = set_[type].name_;
  }
  return ret;
}

static ObPhyOperatorTypeDescSet PHY_OP_TYPE_DESC_SET;
const char *ob_phy_operator_type_str(ObPhyOperatorType type)
{
  return PHY_OP_TYPE_DESC_SET.get_type_str(type);
}

}
}
