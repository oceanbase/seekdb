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
#ifndef SRC_LIBRARY_SRC_LIB_RESTORE_OB_STORAGE_HDFS_JNI_BASE_H_
#define SRC_LIBRARY_SRC_LIB_RESTORE_OB_STORAGE_HDFS_JNI_BASE_H_

#include <hdfs/hdfs.h>

#include "ob_storage_hdfs_cache.h"
#include "lib/restore/ob_i_storage.h"
#include "lib/allocator/page_arena.h"

namespace oceanbase
{
namespace common
{

class ObStorageHdfsJniUtil : public ObIStorageUtil
{
public:
  ObStorageHdfsJniUtil();
  virtual ~ObStorageHdfsJniUtil();
  virtual int open(common::ObObjectStorageInfo *storage_info) override;

  virtual void close() override;

  virtual int head_object_meta(const ObString &uri, ObStorageObjectMetaBase &obj_meta) override;
  
  virtual int is_exist(const ObString &uri, bool &exist) override;
  virtual int get_file_length(const ObString &uri, int64_t &file_length) override;
  virtual int del_file(const ObString &uri) override;
  virtual int batch_del_files(const ObString &uri,
                  hash::ObHashMap<ObString, int64_t> &files_to_delete,
                  ObIArray<int64_t> &failed_files_idx) override;
  virtual int write_single_file(const ObString &uri, const char *buf, const int64_t size) override;
  virtual int mkdir(const ObString &uri) override;
  virtual int list_files(const ObString &uri, ObBaseDirEntryOperator &op) override;
  virtual int list_files(const ObString &uri, ObStorageListCtxBase &list_ctx) override;
  virtual int del_dir(const ObString &uri) override;
  virtual int list_directories(const ObString &uri, ObBaseDirEntryOperator &op) override;
  virtual int is_tagging(const ObString &uri, bool &is_tagging) override;
  virtual int del_unmerged_parts(const ObString &uri) override;

private:
  bool is_opened_;
  ObObjectStorageInfo *storage_info_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObStorageHdfsJniUtil);
};

class ObStorageHdfsBase
{
public:
  ObStorageHdfsBase();
  virtual ~ObStorageHdfsBase();

  virtual void reset();
  virtual int open(const ObString &uri, ObObjectStorageInfo *storage_info);

public:
  bool is_inited() const { return is_inited_; }
  int get_hdfs_file_meta(const ObString &uri, ObStorageObjectMetaBase &meta);
  int parse_namenode_and_path(const ObString &uri_str);
  int get_or_create_fs(const ObString &uri, ObObjectStorageInfo *storage_info);
  int get_or_create_read_file(const ObString &uri);

  const char *get_namenode() const { return namenode_buf_; }
  const char *get_path() const { return path_buf_; }

  hdfsFS get_fs()
  {
    hdfsFS hdfs_fs = nullptr;
    if (OB_NOT_NULL(hdfs_client_)) {
      hdfs_fs = hdfs_client_->get_hdfs_fs();
    }
    return hdfs_fs;
  }

  hdfsFile &get_hdfs_read_file() { return hdfs_read_file_; }

protected:
  int get_hdfs_file_meta_(const ObString &uri, ObStorageObjectMetaBase &meta);

private:
  bool is_inited_;
  bool is_opened_readable_file_;
  bool is_opened_writable_file_;
  hdfsFile hdfs_read_file_;
  ObHdfsFsClient *hdfs_client_;
  common::ObArenaAllocator allocator_;
  char *namenode_buf_;
  char *path_buf_;

private:
  friend class ObStorageHdfsJniUtil;
  DISALLOW_COPY_AND_ASSIGN(ObStorageHdfsBase);
};

class ObStorageHdfsReader : public ObStorageHdfsBase, public ObIStorageReader
{
public:
  ObStorageHdfsReader();
  virtual ~ObStorageHdfsReader();
  virtual void reset() override;
  virtual int open(const ObString &uri,
                   ObObjectStorageInfo *storage_info, const bool head_meta = true) override;
  virtual int pread(char *buf, const int64_t buf_size, const int64_t offset, int64_t &read_size) override;
  virtual int close() override;
  virtual int64_t get_length() const override { return file_length_; }
  virtual bool is_opened() const override { return is_opened_; }

protected:
  bool is_opened_;
  bool has_meta_;
  int64_t file_length_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObStorageHdfsReader);
};

} // common
} // oceanbase

#endif /* SRC_LIBRARY_SRC_LIB_RESTORE_OB_STORAGE_HDFS_JNI_BASE_H_ */
