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

#define USING_LOG_PREFIX SHARE

#include "ob_admin_dump_enum_value_executor.h"

using namespace oceanbase::common;
using namespace oceanbase::share;

namespace oceanbase
{
namespace tools
{

ObAdminDumpEnumValueExecutor::ObAdminDumpEnumValueExecutor()
{
}

ObAdminDumpEnumValueExecutor::~ObAdminDumpEnumValueExecutor()
{
}

int ObAdminDumpEnumValueExecutor::execute(int argc, char *argv[])
{
  UNUSEDx(argc, argv);
  int ret = OB_SUCCESS;

  printf("MODULE ID SEMANTICS\n");

  print_rpc_code();

  return ret;
}

void ObAdminDumpEnumValueExecutor::print_rpc_code()
{
#define PCODE_DEF(name, id) printf("RPC_CODE %d %s\n", id, #name);
#include "rpc/obrpc/ob_rpc_packet_list.h"
#undef PCODE_DEF
}

}
}
