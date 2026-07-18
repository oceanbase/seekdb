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

#ifndef _OCEANBASE_OBSERVER_OB_SERVER_STRUCT_H_
#define _OCEANBASE_OBSERVER_OB_SERVER_STRUCT_H_

#include "share/ob_lease_struct.h"
#include "lib/net/ob_addr.h"
#include "share/ob_cluster_role.h"              // ObClusterRole
#include "share/ob_rpc_struct.h"
#include "share/ob_server_struct.h"

// This file is replaced by share/ob_server_struct.h, 
// PLEASE include "share/ob_server_struct.h" instead
namespace oceanbase
{
namespace observer
{
using ObServiceStatus = share::ObServiceStatus;
using ObServerMode = share::ObServerMode;
using ObGlobalContext = share::ObGlobalContext;
} // end of namespace observer
} // end of namespace oceanbase

#endif // OCEANBASE_OBSERVER_OB_SERVER_STRUCT_H_
