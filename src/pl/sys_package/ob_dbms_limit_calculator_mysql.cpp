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

#define USING_LOG_PREFIX PL
#include "ob_dbms_limit_calculator_mysql.h"

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::pl;
using namespace oceanbase::sql;


namespace oceanbase
{
namespace pl
{
int ObDBMSLimitCalculator::phy_res_calculate_by_logic_res(
    sql::ObExecContext &ctx,
    sql::ParamStore &params,
    common::ObObj &result)
{
  int ret = OB_SUCCESS;
  const int64_t MAX_RES_LEN = 512;
  char* ptr = NULL;
  int64_t pos = 0;
  ObString str_arg;
  ObUserResourceCalculateArg arg;
  ObMinPhyResourceResult res;
  const int64_t curr_tenant_id = MTL_ID();
  if (!is_sys_tenant(curr_tenant_id)) {
    ret = OB_OP_NOT_ALLOW;
    LOG_WARN("only sys tenant can do this", K(ret), K(curr_tenant_id));
    LOG_USER_ERROR(OB_OP_NOT_ALLOW, "Only sys tenant can do this. Operator is");
  } else if (OB_UNLIKELY(2 > params.count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("params not valid", KR(ret), K(params));
  } else if (OB_FAIL(params.at(0).get_varchar(str_arg))) {
    LOG_WARN("get parameter failed", K(ret));
  } else if (str_arg.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(str_arg));
  } else if (OB_ISNULL(ptr = static_cast<char *>(ctx.get_allocator().alloc(MAX_RES_LEN)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate memory failed", K(ret), K(MAX_RES_LEN));
  } else if (OB_FAIL(parse_dict_like_args_(to_cstring(str_arg), arg))) {
    LOG_WARN("parse argument failed", K(ret));
  } else if (OB_FAIL(MTL(ObResourceLimitCalculator *)->get_tenant_min_phy_resource_value(arg, res))) {
    LOG_WARN("get tenant min physical resource needed failed", K(ret));
  } else if (OB_FAIL(get_json_result_(res, ptr, MAX_RES_LEN, pos))) {
    LOG_WARN("get json result failed", K(ret), K(res), K(pos));
  } else {
    params.at(1).set_varchar(ptr, pos);
    LOG_INFO("phy_res_calculate_by_logic_res success", K(arg), K(res),
             K(str_arg), K(params.at(0).get_varchar()), K(params.at(1).get_varchar()));
  }
  return ret;
}

int ObDBMSLimitCalculator::phy_res_calculate_by_unit(
    sql::ObExecContext &ctx,
    sql::ParamStore &params,
    common::ObObj &result)
{
  int ret = OB_SUCCESS;
  ObAddr addr;
  int64_t tenant_id = 0;
  ObString addr_str;
  const int64_t MAX_RES_LEN = 2048;
  ObMinPhyResourceResult res;
  char* ptr = NULL;
  int64_t pos = 0;
  int64_t timeout = -1;
  const int64_t curr_tenant_id = MTL_ID();
  if (!is_sys_tenant(curr_tenant_id)) {
    ret = OB_OP_NOT_ALLOW;
    LOG_WARN("only sys tenant can do this", K(ret), K(curr_tenant_id));
    LOG_USER_ERROR(OB_OP_NOT_ALLOW, "Only sys tenant can do this. Operator is");
  } else if (OB_UNLIKELY(3 > params.count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("params not valid", KR(ret), K(params));
  } else if (FALSE_IT(tenant_id = params.at(0).get_int())) {
  } else if (FALSE_IT(addr_str = params.at(1).get_varchar())) {
  } else if (OB_FAIL(addr.parse_from_string(addr_str))) {
    LOG_WARN("parse address failed", K(ret), K(addr_str));
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(ptr = static_cast<char *>(ctx.get_allocator().alloc(MAX_RES_LEN)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate memory failed", K(ret), K(MAX_RES_LEN));
  } else if (OB_ISNULL(GCTX.srv_rpc_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("rpc_proxy or session is null", K(ret), K(GCTX.srv_rpc_proxy_));
  } else if (0 >= (timeout = THIS_WORKER.get_timeout_remain())) {
    ret = OB_TIMEOUT;
    LOG_WARN("query timeout is reached", K(ret), K(timeout));
  } else if (OB_FAIL(GCTX.srv_rpc_proxy_->to(addr)
                     .timeout(timeout)
                     .by(tenant_id)
                     .phy_res_calculate_by_unit(tenant_id, res))) {
    LOG_WARN("failed to update local stat cache caused by unknow error",
             K(ret), K(addr), K(timeout), K(tenant_id));
    // rewrite the error code to make user recheck the argument and retry.
    // we should never retry here because there is something error.
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "calculate physical resource needed by unit. "
                                        "Please check the tenant id and observer address and retry.");
  } else if (OB_FAIL(get_json_result_(tenant_id, addr, res, ptr, MAX_RES_LEN, pos))) {
    LOG_WARN("get json result failed", K(ret), K(addr), K(res), K(pos), K(MAX_RES_LEN));
  } else {
    params.at(2).set_varchar(ptr, pos);
    LOG_INFO("phy_res_calculate_by_unit success", K(params.at(0).get_int()),
             K(params.at(1).get_varchar()), K(params.at(2).get_varchar()));
  }
  return ret;
}

int ObDBMSLimitCalculator::phy_res_calculate_by_standby_tenant(
    sql::ObExecContext &ctx,
    sql::ParamStore &params,
    common::ObObj &result)
{
  int ret = OB_NOT_SUPPORTED;
  LOG_WARN("standby tenant not supported in lite version", KR(ret));
  return ret;
}

int ObDBMSLimitCalculator::parse_dict_like_args_(
    const char* ptr,
    ObUserResourceCalculateArg &arg)
{
  int ret = OB_SUCCESS;
  char key[50] = "";
  int64_t value = 0;
  // parse the argument like: "ls: 1, tablet: 2, xxxx"
  while (OB_SUCC(ret) && sscanf(ptr, "%[^:]: %ld", key, &value) == 2) {
    int64_t type = get_logic_res_type_by_name(key);
    if (!is_valid_logic_res_type(type)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(ret), K(type), K(key));
    } else if (OB_FAIL(arg.set_type_value(type, value))) {
      LOG_WARN("set type value failed", K(ret), K(type), K(value));
    }
    while (*ptr != '\0' && *ptr != ',') ptr++;
    while (*ptr != '\0' && (*ptr == ' ' || *ptr == ',')) ptr++;
  }
  return ret;
}

int ObDBMSLimitCalculator::get_json_result_(
    const ObMinPhyResourceResult &res,
    char *buf,
    const int64_t buf_len,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t i = PHY_RESOURCE_MEMSTORE;
  int64_t value = 0;
  if (OB_FAIL(res.get_type_value(i, value))) {
    LOG_WARN("get_type_value failed", K(ret), K(get_phy_res_type_name(i)), K(value));
  } else if (OB_FAIL(databuff_printf(buf,
                                     buf_len,
                                     pos,
                                     "[{\"physical_resource_name\": \"%s\", \"min_value\": \"%ld\"}",
                                     get_phy_res_type_name(i),
                                     value))) {
    LOG_WARN("get result buffer failed", K(ret), K(pos), K(buf_len));
  } else {
    // get next type
    i++;
    for (; OB_SUCC(ret) && i < MAX_PHY_RESOURCE; i++) {
      if (OB_FAIL(res.get_type_value(i, value))) {
        LOG_WARN("get_type_value failed", K(ret), K(get_phy_res_type_name(i)), K(value));
      } else if (OB_FAIL(databuff_printf(buf,
                                         buf_len,
                                         pos,
                                         ", {\"physical_resource_name\": \"%s\", \"min_value\": \"%ld\"}",
                                         get_phy_res_type_name(i),
                                         value))) {
        LOG_WARN("get result buffer failed", K(ret), K(pos), K(buf_len));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(databuff_printf(buf,
                                  buf_len,
                                  pos,
                                  "]"))) {
        LOG_WARN("get result buffer failed", K(ret), K(pos), K(buf_len));
      }
    }
  }
  return ret;
}

int ObDBMSLimitCalculator::get_json_result_(
    const int64_t tenant_id,
    const ObAddr &addr,
    const ObMinPhyResourceResult &res,
    char *buf,
    const int64_t buf_len,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  char ip[MAX_IP_ADDR_LENGTH] = "";
  int64_t port = 0;
  int64_t value = 0;
  int64_t i = PHY_RESOURCE_MEMSTORE;
  if (!addr.ip_to_string(ip, MAX_IP_ADDR_LENGTH)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get ip string failed", K(ret), K(addr));
  } else if (FALSE_IT(port = addr.get_port())) {
  } else if (OB_FAIL(res.get_type_value(i, value))) {
    LOG_WARN("get_type_value failed", K(ret), K(get_phy_res_type_name(i)), K(value));
  } else if (OB_FAIL(databuff_printf(buf,
                                     buf_len,
                                     pos,
                                     "[{\"svr_ip\": \"%s\", \"svr_port\": \"%ld\", \"tenant_id\" : \"%ld\", \"physical_resource_name\": \"%s\", \"min_value\": \"%ld\"}",
                                     ip,
                                     port,
                                     tenant_id,
                                     get_phy_res_type_name(i),
                                     value))) {
    LOG_WARN("get result buffer failed", K(ret), K(pos), K(buf_len));
  } else {
    // get next type
    i++;
    for (; OB_SUCC(ret) && i < MAX_PHY_RESOURCE; i++) {
      if (OB_FAIL(res.get_type_value(i, value))) {
        LOG_WARN("get_type_value failed", K(ret), K(get_phy_res_type_name(i)), K(value));
      } else if (OB_FAIL(databuff_printf(buf,
                                         buf_len,
                                         pos,
                                         ", {\"svr_ip\": \"%s\", \"svr_port\": \"%ld\", \"tenant_id\" : \"%ld\", \"physical_resource_name\": \"%s\", \"min_value\": \"%ld\"}",
                                         ip,
                                         port,
                                         tenant_id,
                                         get_phy_res_type_name(i),
                                         value))) {
        LOG_WARN("get result buffer failed", K(ret), K(pos), K(buf_len));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(databuff_printf(buf,
                                  buf_len,
                                  pos,
                                  "]"))) {
        LOG_WARN("get result buffer failed", K(ret), K(pos), K(buf_len));
      }
    }
  }
  return ret;
}

} // namespace pl
} // namespace oceanbase
