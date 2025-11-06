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

#include <gtest/gtest.h>
#include "logservice/leader_coordinator/table_accessor.h"

namespace oceanbase
{
using namespace share;
using namespace common;
using namespace palf;
using namespace logservice;
using namespace logservice::coordinator;
namespace unittest
{

class TestLsElectionReferenceInfoRow : public ::testing::Test
{
public:
  TestLsElectionReferenceInfoRow() {}
};

// TEST_F(TestLsElectionReferenceInfoRow, normal)
// {
//   LsElectionReferenceInfoRow row(1, ObLSID(1));
//   ObArray<ObArray<ObString>> zone_list_list;
//   ObArray<ObString> zone_list1, zone_list2, zone_list3;
//   zone_list1.push_back("z1");
//   zone_list2.push_back("z2");
//   zone_list2.push_back("z3");
//   zone_list3.push_back("z4");
//   zone_list3.push_back("z5");
//   zone_list_list.push_back(zone_list1);
//   zone_list_list.push_back(zone_list2);
//   zone_list_list.push_back(zone_list3);
//   ObArray<ObAddr> remove_member_list;
//   remove_member_list.push_back(ObAddr(ObAddr::VER::IPV4, "123.1.1.1", 1091));
//   remove_member_list.push_back(ObAddr(ObAddr::VER::IPV4, "123.1.1.1", 1092));
//   remove_member_list.push_back(ObAddr(ObAddr::VER::IPV4, "123.1.1.1", 1093));
//   ObArray<ObString> remove_reason;
//   remove_reason.push_back(ObString("reason1"));
//   remove_reason.push_back(ObString("reason1"));
//   remove_reason.push_back(ObString("reason1"));
//   ASSERT_EQ(OB_SUCCESS, row.set(ObLSID(1), zone_list_list, ObAddr(ObAddr::VER::IPV4, "123.1.1.1", 1090), remove_member_list, remove_reason));
//   COORDINATOR_LOG(INFO, "debug", K(row));
// }

}
}

int main(int argc, char **argv)
{
  system("rm -rf etc run log wallet store");
  system("rm -rf test_ls_election_reference_info.log");
  oceanbase::common::ObLogger &logger = oceanbase::common::ObLogger::get_logger();
  logger.set_file_name("test_ls_election_reference_info.log", false, false);
  logger.set_log_level("INFO");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
