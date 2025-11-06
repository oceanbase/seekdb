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

#ifndef OBDEV_SRC_SHARE_EXTERNAL_TABLE_OB_HDFS_TABLE_DEVICE_H
#define OBDEV_SRC_SHARE_EXTERNAL_TABLE_OB_HDFS_TABLE_DEVICE_H

#include "lib/restore/ob_object_device.h"
#include "share/external_table/ob_hdfs_storage_info.h"


namespace oceanbase
{
namespace share
{
class ObHDFSTableDevice final : public common::ObObjectDevice
{
public:
  ObHDFSTableDevice();
  ~ObHDFSTableDevice();

  virtual void destroy() override;
  virtual int setup_storage_info(const ObIODOpts &opts) override;
  virtual common::ObObjectStorageInfo &get_storage_info() { return external_storage_info_; }

private:
  ObHDFSStorageInfo external_storage_info_;
  DISALLOW_COPY_AND_ASSIGN(ObHDFSTableDevice);
};

} // namespace share
} // namespace oceanbase
# endif /* OBDEV_SRC_SHARE_EXTERNAL_TABLE_OB_HDFS_TABLE_DEVICE_H */
