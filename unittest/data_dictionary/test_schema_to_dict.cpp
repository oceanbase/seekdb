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


#define private public
#undef private
#include "data_dict_test_utils.h"

using namespace oceanbase;
using namespace common;
using namespace share;
using namespace share::schema;

namespace oceanbase
{
namespace datadict
{

TEST(ObDictTenantMeta, schema_to_meta)
{
  ObMockSchemaBuilder schema_builder;
  ObArenaAllocator allocator;
  ObTenantSchema tenant_schema;
  EXPECT_EQ(OB_SUCCESS, schema_builder.build_tenant_schema(tenant_schema));
  ObDictTenantMeta tenant_meta(&allocator);
  ObLSArray ls_arr;
  EXPECT_EQ(OB_SUCCESS, ls_arr.push_back(ObLSID(1)));
  EXPECT_EQ(OB_SUCCESS, ls_arr.push_back(ObLSID(1001)));
  EXPECT_EQ(OB_SUCCESS, tenant_meta.init_with_ls_info(tenant_schema, ls_arr));
  EXPECT_TRUE(tenant_schema.get_tenant_name_str() == tenant_meta.tenant_name_);
}

} // namespace datadict
} // namespace oceanbase

int main(int argc, char **argv)
{
  // testing::FLAGS_gtest_filter = "DO_NOT_RUN";
  system("rm -f test_ob_schema_to_dict.log");
  ObLogger &logger = ObLogger::get_logger();
  bool not_output_obcdc_log = true;
  logger.set_file_name("test_ob_schema_to_dict.log", not_output_obcdc_log, false);
  logger.set_log_level(OB_LOG_LEVEL_DEBUG);
  logger.set_mod_log_levels("ALL.*:DEBUG, META_DICT.*:DEBUG");
  logger.set_enable_async_log(false);
  testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}
