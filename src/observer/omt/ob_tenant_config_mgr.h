/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#ifndef OCEANBASE_TENANT_CONFIG_MGR_H_
#define OCEANBASE_TENANT_CONFIG_MGR_H_

#include "lib/function/ob_function.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/string/ob_sql_string.h"
#include "lib/container/ob_array.h"
#include "lib/lock/ob_drw_lock.h"
#include "share/config/ob_reload_config.h"
#include "share/config/ob_config_helper.h"
#include "share/config/ob_config_manager.h"
#include "share/ob_lease_struct.h"
#include "share/rc/ob_context.h"
#include "share/rc/ob_tenant_base.h"

namespace oceanbase {
namespace obrpc
{
class ObTenantConfigArg;
}
namespace omt {

class ObTenantConfigGuard {
public:
  ObTenantConfigGuard();
  ObTenantConfigGuard(ObServerConfig *config);
  virtual ~ObTenantConfigGuard();

  ObTenantConfigGuard(const ObTenantConfigGuard &guard) = delete;
  ObTenantConfigGuard & operator=(const ObTenantConfigGuard &guard) = delete;

  void set_config(ObServerConfig *config);
  bool is_valid() { return nullptr != config_; }
  /*
   * Requires: check is_valid().
   */
  ObServerConfig *operator->() { return config_; }
private:
  ObServerConfig *config_;
};

#define TENANT_CONF(tenant_id) &GCONF
#define TENANT_CONF_TIL(tenant_id) &GCONF

}
}

#endif
