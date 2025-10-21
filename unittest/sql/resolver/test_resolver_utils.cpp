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
#include "sql/resolver/ob_resolver_utils.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{
class TestResovlerUtils : public ::testing::Test
{
public:
  TestResovlerUtils() = default;
  ~TestResovlerUtils() = default;
};

TEST_F(TestResovlerUtils, check_secure_path)
{
  int ret = OB_SUCCESS;

  {
    ObString secure_file_priv("/");
    ObString directory_path1("/tmp");
    ret = ObResolverUtils::check_secure_path(secure_file_priv, directory_path1);
    ASSERT_EQ(OB_SUCCESS, ret);
    ObString directory_path2("/Tmp");
    ret = ObResolverUtils::check_secure_path(secure_file_priv, directory_path2);
    ASSERT_EQ(OB_SUCCESS, ret);
    ObString directory_path3("/");
    ret = ObResolverUtils::check_secure_path(secure_file_priv, directory_path3);
    ASSERT_EQ(OB_SUCCESS, ret);
  }

  {
    ObString secure_file_priv("null");
    ObString directory_path1("/tmp");
    ret = ObResolverUtils::check_secure_path(secure_file_priv, directory_path1);
    ASSERT_EQ(OB_ERR_NO_PRIVILEGE, ret);
    ObString directory_path2("/");
    ret = ObResolverUtils::check_secure_path(secure_file_priv, directory_path2);
    ASSERT_EQ(OB_ERR_NO_PRIVILEGE, ret);
  }

  {
    ObString secure_file_priv("/tmp");
    ObString directory_path1("/tmp");
    ret = ObResolverUtils::check_secure_path(secure_file_priv, directory_path1);
    ASSERT_EQ(OB_SUCCESS, ret);
    ObString directory_path2("/Tmp");
    ret = ObResolverUtils::check_secure_path(secure_file_priv, directory_path2);
    ASSERT_EQ(OB_ERR_NO_PRIVILEGE, ret);
    ObString directory_path3("/tmp/test");
    ret = ObResolverUtils::check_secure_path(secure_file_priv, directory_path3);
    ASSERT_EQ(OB_SUCCESS, ret);
    ObString directory_path4("/home");
    ret = ObResolverUtils::check_secure_path(secure_file_priv, directory_path4);
    ASSERT_EQ(OB_ERR_NO_PRIVILEGE, ret);
    ObString directory_path5("/tmpabc");
    ret = ObResolverUtils::check_secure_path(secure_file_priv, directory_path5);
    ASSERT_EQ(OB_ERR_NO_PRIVILEGE, ret);
  }

  {
    ObString secure_file_priv("/tmp/");
    ObString directory_path1("/tmp");
    ret = ObResolverUtils::check_secure_path(secure_file_priv, directory_path1);
    ASSERT_EQ(OB_SUCCESS, ret);
    ObString directory_path2("/Tmp");
    ret = ObResolverUtils::check_secure_path(secure_file_priv, directory_path2);
    ASSERT_EQ(OB_ERR_NO_PRIVILEGE, ret);
    ObString directory_path3("/tmp/test");
    ret = ObResolverUtils::check_secure_path(secure_file_priv, directory_path3);
    ASSERT_EQ(OB_SUCCESS, ret);
    ObString directory_path4("/home");
    ret = ObResolverUtils::check_secure_path(secure_file_priv, directory_path4);
    ASSERT_EQ(OB_ERR_NO_PRIVILEGE, ret);
    ObString directory_path5("/tmpabc");
    ret = ObResolverUtils::check_secure_path(secure_file_priv, directory_path5);
    ASSERT_EQ(OB_ERR_NO_PRIVILEGE, ret);
  }

  {
    system("mkdir -p /tmp/test_resolver_utils_check_secure_path");
    ObString secure_file_priv("/tmp/test_resolver_utils_check_secure_path");
    ObString directory_path1("/");
    ret = ObResolverUtils::check_secure_path(secure_file_priv, directory_path1);
    ASSERT_EQ(OB_ERR_NO_PRIVILEGE, ret);
    ObString directory_path2("/a");
    ret = ObResolverUtils::check_secure_path(secure_file_priv, directory_path2);
    ASSERT_EQ(OB_ERR_NO_PRIVILEGE, ret);
    ObString directory_path3("/tmp");
    ret = ObResolverUtils::check_secure_path(secure_file_priv, directory_path3);
    ASSERT_EQ(OB_ERR_NO_PRIVILEGE, ret);
    ObString directory_path4("/tmp/test_resolver_utils_check_secure_path");
    ret = ObResolverUtils::check_secure_path(secure_file_priv, directory_path4);
    ASSERT_EQ(OB_SUCCESS, ret);
    ObString directory_path5("/TMP/test_resolver_utils_check_secure_path");
    ret = ObResolverUtils::check_secure_path(secure_file_priv, directory_path5);
    ASSERT_EQ(OB_ERR_NO_PRIVILEGE, ret);
    ObString directory_path6("/tmp/test_resolver_utils_Check_Secure_Path");
    ret = ObResolverUtils::check_secure_path(secure_file_priv, directory_path6);
    ASSERT_EQ(OB_ERR_NO_PRIVILEGE, ret);
    ObString directory_path7("/tmp/test_resolver_utils_check_secure_path/hehe");
    ret = ObResolverUtils::check_secure_path(secure_file_priv, directory_path7);
    ASSERT_EQ(OB_SUCCESS, ret);
    system("rm -rf /tmp/test_resolver_utils_check_secure_path");
  }
}
} // namespace sql
} // namespace oceanbase

int main(int argc, char **argv)
{
  system("rm -f test_resolver_utils.log*");
  OB_LOGGER.set_file_name("test_resolver_utils.log", true);
  OB_LOGGER.set_log_level("INFO");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
