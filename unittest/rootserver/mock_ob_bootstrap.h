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

namespace oceanbase {
namespace rootserver {

class MockObBaseBootstrap : public ObBaseBootstrap {
 public:
  MOCK_METHOD1(check_bootstrap_rs_list,
      int(const obrpc::ObServerInfoList &rs_list));
  MOCK_METHOD1(create_partition,
      int(const uint64_t table_id));
};

}  // namespace rootserver
}  // namespace oceanbase

namespace oceanbase {
namespace rootserver {

class MockObPreBootstrap : public ObPreBootstrap {
 public:
  MOCK_METHOD1(prepare_bootstrap,
      int(common::ObAddr &master_rs));
  MOCK_METHOD1(check_is_all_server_empty,
      int(bool &is_empty));
  MOCK_METHOD1(wait_elect_master_partition,
      int(common::ObAddr &master_rs));
};

}  // namespace rootserver
}  // namespace oceanbase

namespace oceanbase {
namespace rootserver {

class MockObBootstrap : public ObBootstrap {
 public:
  MOCK_METHOD0(execute_bootstrap,
      int());
  MOCK_METHOD1(check_is_already_bootstrap,
      int(bool &is_bootstrap));
  MOCK_METHOD0(create_core_tables,
      int());
  MOCK_METHOD0(create_sys_tables,
      int());
  MOCK_METHOD0(create_virtual_tables,
      int());
  MOCK_METHOD0(init_system_data,
      int());
  MOCK_METHOD0(init_all_sys_stat,
      int());
  MOCK_METHOD0(wait_all_rs_online,
      int());
};

}  // namespace rootserver
}  // namespace oceanbase
