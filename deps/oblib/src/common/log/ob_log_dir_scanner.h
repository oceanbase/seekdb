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

#ifndef OCEANBASE_SHARE_LOG_OB_LOG_DIR_SCANNER_
#define OCEANBASE_SHARE_LOG_OB_LOG_DIR_SCANNER_

#include "lib/container/ob_vector.h"

namespace oceanbase
{
namespace common
{
struct ObSimpleLogFile
{
  char name[OB_MAX_FILE_NAME_LENGTH];
  uint64_t id;

  enum FileType
  {
    UNKNOWN = 1,
    LOG = 2,
    CKPT = 3
  };

  int assign(const char *filename, FileType &type);
  bool is_log_id(const char *str) const ;
  uint64_t str_to_uint64(const char *str) const;
  bool operator< (const ObSimpleLogFile &r) const;
};

class ObLogDirScanner
{
public:
  ObLogDirScanner();
  virtual ~ObLogDirScanner();

  /**
   * ObLogScanner will scan folder and sort the file by file id.
   * Return the min and max id, and checkpoint id
   *
   * !!! An exception is the discontinuous log files. In this case, init return OB_DISCONTINUOUS_LOG
   *
   * @param [in] log_dir directory path
   * @return OB_SUCCESS success
   * @return OB_DISCONTINUOUS_LOG scan meet discontinuous log files
   * @return otherwise fail
   */

  /// @brief get minimum commit log id
  /// @retval OB_SUCCESS
  /// @retval OB_ENTRY_NOT_EXIST no commit log in directory

  /// @brief get maximum commit log id
  /// @retval OB_SUCCESS
  /// @retval OB_ENTRY_NOT_EXIST no commit log in directory

  /// @brief get maximum checkpoint id
  /// @retval OB_SUCCESS
  /// @retval OB_ENTRY_NOT_EXIST no commit log in directory

  bool has_log() const;
  void reset();

private:
  /**
   * Find all log files under directory
   */
  int search_log_dir_(const char *log_dir);

  /**
   * Check if logs are continuous
   * Return min and max file id
   */
  int check_continuity_(const ObVector<ObSimpleLogFile> &files, uint64_t &min_file_id,
                        uint64_t &max_file_id);

  inline int check_inner_stat() const
  {
    int ret = OB_SUCCESS;
    if (!is_init_) {
      SHARE_LOG(ERROR, "ObLogDirScanner has not been initialized.");
      ret = OB_NOT_INIT;
    }
    return ret;
  }

  DISALLOW_COPY_AND_ASSIGN(ObLogDirScanner);

private:
  uint64_t min_log_id_;
  uint64_t max_log_id_;
  uint64_t max_ckpt_id_;
  bool has_log_;
  bool has_ckpt_;
  bool is_init_;
};
} // end namespace common
} // end namespace oceanbase

#endif // OCEANBASE_SHARE_LOG_OB_LOG_DIR_SCANNER_
