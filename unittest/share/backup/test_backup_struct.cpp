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

#include <gtest/gtest.h>
#define private public
#include "share/backup/ob_backup_path.h"

using namespace oceanbase;
using namespace common;
using namespace share;

TEST(ObBackupUtils, check_is_tmp_file)
{
  bool is_tmp_file = false;
  ObString file_name_1 = "file_name.tmp.123456";
  ASSERT_EQ(OB_SUCCESS, ObBackupUtils::check_is_tmp_file(file_name_1, is_tmp_file));
  ASSERT_TRUE(is_tmp_file);
  ObString file_name_2 = "file_name.TMP.123456";
  ASSERT_EQ(OB_SUCCESS, ObBackupUtils::check_is_tmp_file(file_name_2, is_tmp_file));
  ASSERT_FALSE(is_tmp_file);
  ObString file_name_3 = "file_name.tmp";
  ASSERT_EQ(OB_SUCCESS, ObBackupUtils::check_is_tmp_file(file_name_3, is_tmp_file));
  ASSERT_FALSE(is_tmp_file);
  ObString file_name_4 = "file_name.tmp.";
  ASSERT_EQ(OB_SUCCESS, ObBackupUtils::check_is_tmp_file(file_name_4, is_tmp_file));
  ASSERT_FALSE(is_tmp_file);
  ObString file_name_5 = "file_name";
  ASSERT_EQ(OB_SUCCESS, ObBackupUtils::check_is_tmp_file(file_name_5, is_tmp_file));
  ASSERT_FALSE(is_tmp_file);
  ObString file_name_6 = "file_name.123456";
  ASSERT_EQ(OB_SUCCESS, ObBackupUtils::check_is_tmp_file(file_name_6, is_tmp_file));
  ASSERT_FALSE(is_tmp_file);
}

TEST(ObBackupDest, nfs)
{
  const char *backup_test = "file:///backup_dir/?&delete_mode=tagging";
  ObBackupDest dest;
  ObBackupDest dest1;
  ObBackupDest dest2;
  ObBackupDest dest3;
  ASSERT_EQ(OB_INVALID_BACKUP_DEST, dest.set(backup_test));
  ASSERT_EQ(OB_SUCCESS, dest.set_storage_path(ObString(backup_test)));
  backup_test = "file:///backup_dir/";

  char backup_dest_str[OB_MAX_BACKUP_DEST_LENGTH] = { 0 };
  char backup_path_str[OB_MAX_BACKUP_DEST_LENGTH] = { 0 };
  ASSERT_EQ(OB_SUCCESS, dest.set(backup_test));
  ASSERT_EQ(OB_SUCCESS, dest1.set(backup_test));
  ASSERT_EQ(OB_SUCCESS, dest3.set_storage_path(ObString(backup_test)));
  LOG_INFO("dump backup dest", K(dest), K(dest.get_root_path()), KPC(dest.get_storage_info()));
  LOG_INFO("dump backup dest", K(dest3), K(dest.get_root_path()), KPC(dest.get_storage_info()));
  ASSERT_EQ(0, strcmp(dest.root_path_, "file:///backup_dir"));
  ASSERT_TRUE(dest.storage_info_->device_type_ == 0);
  ObString backup_str("file:///backup_dir/");
  ASSERT_EQ(OB_SUCCESS, dest2.set(backup_str));
  ASSERT_EQ(0, strcmp(dest.root_path_, "file:///backup_dir"));

  ASSERT_EQ(OB_SUCCESS, dest.get_backup_dest_str(backup_dest_str, sizeof(backup_dest_str)));
  ASSERT_EQ(0, strcmp(backup_dest_str, "file:///backup_dir"));
  ASSERT_EQ(OB_SUCCESS, dest.get_backup_path_str(backup_path_str, sizeof(backup_path_str)));
  ASSERT_EQ(0, strcmp(backup_path_str, "file:///backup_dir")); 
  ASSERT_TRUE(dest.is_root_path_equal(dest1));
  bool is_equal = false;
  ASSERT_EQ(OB_SUCCESS, dest.is_backup_path_equal(dest1, is_equal));
  ASSERT_TRUE(is_equal);
  is_equal = false;
  ASSERT_EQ(OB_SUCCESS, dest.is_backup_path_equal(dest3, is_equal));
  ASSERT_TRUE(is_equal);
  ASSERT_TRUE(dest == dest1);
  ASSERT_EQ(OB_SUCCESS, dest2.deep_copy(dest));
  ASSERT_EQ(0, strcmp(dest.root_path_, "file:///backup_dir"));
}

TEST(ObBackupDestAttributeParser, parse_option)
{
  int64_t max_iops = 0;
  int64_t max_bandwidth = 0;
  const char *option = "max_iops=1000&max_bandwidth=1024000b";
  ObBackupDestAttribute dest_option;
  dest_option.reset();
  ASSERT_EQ(OB_SUCCESS, ObBackupDestAttributeParser::parse(option, dest_option));
  ASSERT_EQ(1000, dest_option.max_iops_);
  ASSERT_EQ(1024000, dest_option.max_bandwidth_);

  const char *option_1 = "max_iops=1000";
  dest_option.reset();
  ASSERT_EQ(OB_SUCCESS, ObBackupDestAttributeParser::parse(option_1, dest_option));
  ASSERT_EQ(1000, dest_option.max_iops_);
  ASSERT_EQ(0, dest_option.max_bandwidth_);

  const char *option_2 = "max_bandwidth=10000b";
  dest_option.reset();
  ASSERT_EQ(OB_SUCCESS, ObBackupDestAttributeParser::parse(option_2, dest_option));
  ASSERT_EQ(0, dest_option.max_iops_);
  ASSERT_EQ(10000, dest_option.max_bandwidth_);

  const char *option_3 = "";
  dest_option.reset();
  ASSERT_EQ(-4002, ObBackupDestAttributeParser::parse(option_3, dest_option));
}

TEST(ObBackupDestAttributeParser, parse_acces_info)
{
  const char *option = "access_id=AAA&access_key=BBB";
  ObBackupDestAttribute dest_option;
  dest_option.reset();
  ASSERT_EQ(OB_SUCCESS, ObBackupDestAttributeParser::parse(option, dest_option));
  ASSERT_EQ(0, strcmp(dest_option.access_id_, "AAA"));
  ASSERT_EQ(0, strcmp(dest_option.access_key_, "BBB"));
}

int main(int argc, char **argv)
{
  system("rm -f test_backup_struct.log*");
  OB_LOGGER.set_file_name("test_backup_struct.log", true, true);
  OB_LOGGER.set_log_level("INFO");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
