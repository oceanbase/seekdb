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
#include "share/io/ob_io_manager.h"
#define private public
#include "share/ob_device_manager.h"
#undef private

using namespace oceanbase::common;

class TestDeviceManager: public ::testing::Test
{
public:
  TestDeviceManager() {
  }
  virtual ~TestDeviceManager(){}
  virtual void SetUp()
  {
  }
  virtual void TearDown()
  {
  }
  static void SetUpTestCase()
  {
    ASSERT_EQ(OB_SUCCESS, ObDeviceManager::get_instance().init_devices_env());
  }
  static void TearDownTestCase()
  {
  }
protected:
  // disallow copy
private:
  DISALLOW_COPY_AND_ASSIGN(TestDeviceManager);
};

TEST_F(TestDeviceManager, test_device_manager)
{
  ASSERT_EQ(OB_SUCCESS, ObIOManager::get_instance().init());
  ObDeviceManager &manager = ObDeviceManager::get_instance();
  int max_dev_num = ObDeviceManager::MAX_DEVICE_INSTANCE;
  ObIODevice* device_handle[2*max_dev_num];
  ObString storage_prefix_local(OB_LOCAL_PREFIX);
  manager.destroy();
  ASSERT_EQ(OB_SUCCESS, manager.init_devices_env());
  
  int32_t device_num = 0;
  int32_t device_map_cnt = 0;
  ObIODevice* tmp_dev_handle = NULL;
  MEMSET(device_handle, 0 , sizeof(ObIODevice*)*2*max_dev_num);
  const ObStorageIdMod storage_id_mode(0, ObStorageUsedMod::STORAGE_USED_DATA);

  //all the device is same
  for (int i = 0; i < max_dev_num; i++ ) {
    ASSERT_EQ(OB_SUCCESS, ObDeviceManager::get_local_device(storage_prefix_local, storage_id_mode, device_handle[i]));
    if (0 != i) {
      ASSERT_EQ(tmp_dev_handle, device_handle[i]);
    } else {
      tmp_dev_handle = device_handle[i];
      ASSERT_EQ(OB_SUCCESS, ObIOManager::get_instance().add_device_channel(device_handle[i],
                                                                           16/*async_channel_count*/,
                                                                           2/*sync_channel_count*/,
                                                                           1024/*max_io_depth*/));
    }
  }
  device_num = manager.get_device_cnt();
  ASSERT_EQ(1, device_num);
  //release all the device
  for (int i = 0; i < max_dev_num; i++) {
    ASSERT_EQ(OB_SUCCESS, manager.release_device(device_handle[i]));
  }
  device_num = manager.get_device_cnt();
  ASSERT_EQ(1, device_num); //since we do not release automatic

  //MAX_DEVICE_INSTANCE different deivce
  for (int i = 0; i < max_dev_num; i++ ) {
    ObStorageIdMod tmp_storage_id_mod(i, ObStorageUsedMod::STORAGE_USED_DATA);
    ASSERT_EQ(OB_SUCCESS, ObDeviceManager::get_local_device(
        storage_prefix_local, tmp_storage_id_mod, device_handle[i]));
    //all the device is not same 
    if (NULL != tmp_dev_handle) {
      // ASSERT_TRUE(device_handle[i] != tmp_dev_handle);
    }
    tmp_dev_handle = device_handle[i];
  }
  device_num = manager.get_device_cnt();
  ASSERT_EQ(max_dev_num, device_num);
   
  //exceed MAX_DEVICE_INSTANCE device, should fail
  ObStorageIdMod max_storage_id_mod(max_dev_num, ObStorageUsedMod::STORAGE_USED_DATA);
  ASSERT_EQ(OB_OUT_OF_ELEMENT, ObDeviceManager::get_local_device(
      storage_prefix_local, max_storage_id_mod, tmp_dev_handle));
  //release some and get again, should suc(this device ref should be 0)
  ASSERT_EQ(OB_SUCCESS, manager.release_device(device_handle[0]));
  //get this device again
  ObStorageIdMod min_storage_id_mod(0, ObStorageUsedMod::STORAGE_USED_DATA);
  ASSERT_EQ(OB_SUCCESS, ObDeviceManager::get_local_device(
      storage_prefix_local, min_storage_id_mod, device_handle[0]));
  //copy device handle, test double release scenario
  tmp_dev_handle = device_handle[0];
  ASSERT_EQ(OB_SUCCESS, manager.release_device(device_handle[0]));
  //double release scenario, since the ref is 0, can not release again
  ASSERT_EQ(OB_INVALID_ARGUMENT, manager.release_device(tmp_dev_handle));
  //the device handle has been reset, so will be a null pointer error
  ASSERT_EQ(OB_INVALID_ARGUMENT, manager.release_device(device_handle[0]));               
  ASSERT_EQ(OB_SUCCESS, ObDeviceManager::get_local_device(
      storage_prefix_local, max_storage_id_mod, device_handle[0]));
  device_num = manager.get_device_cnt();
  ASSERT_EQ(max_dev_num, device_num);
  manager.destroy();
  device_num = manager.get_device_cnt();
  ASSERT_EQ(0, device_num);
  ASSERT_EQ(OB_SUCCESS, manager.init_devices_env());

  //get again
  for (int i = 0; i < max_dev_num; i++ ) {
    uint64_t storage_id = 0;
    if ( i >= max_dev_num/2) {
      storage_id = i;
    }
    
    ObStorageIdMod tmp_storage_id_mod(storage_id, ObStorageUsedMod::STORAGE_USED_DATA);
    ASSERT_EQ(OB_SUCCESS, ObDeviceManager::get_local_device(
        storage_prefix_local, tmp_storage_id_mod, device_handle[i]));
  }
  device_num = manager.get_device_cnt();
  ASSERT_EQ(max_dev_num/2 + 1, device_num);


  manager.destroy();
  ASSERT_EQ(0, manager.get_device_cnt());
  ASSERT_EQ(OB_SUCCESS, manager.init_devices_env());
}

int main(int argc, char **argv)
{
  system("rm -f test_storage_device_manager.log");
  OB_LOGGER.set_file_name("test_storage_device_manager.log");
  OB_LOGGER.set_log_level("INFO");
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}
