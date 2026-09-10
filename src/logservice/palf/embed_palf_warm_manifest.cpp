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

#define USING_LOG_PREFIX PALF

#ifdef OB_BUILD_EMBED_MODE

#include "embed_palf_warm_manifest.h"
#include "lib/checksum/ob_crc64.h"
#include "lib/file/file_directory_utils.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/time/ob_time_utility.h"
#include "lib/utility/ob_macro_utils.h"
#include "share/ob_errno.h"
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace oceanbase
{
namespace palf
{

#ifndef _WIN32
namespace
{
int build_manifest_path_(const char *log_stream_dir, const char *file_name, char *path, const int64_t path_len)
{
  int ret = OB_SUCCESS;
  int pret = 0;
  if (OB_ISNULL(log_stream_dir) || OB_ISNULL(path) || path_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (0 > (pret = snprintf(path, path_len, "%s/%s", log_stream_dir, file_name))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (pret >= path_len) {
    ret = OB_BUF_NOT_ENOUGH;
  }
  return ret;
}

int64_t calc_manifest_payload_checksum_(const EmbedPalfWarmManifest &manifest)
{
  const char *payload = reinterpret_cast<const char *>(&manifest.write_ts_us_);
  const int64_t payload_len = sizeof(manifest.write_ts_us_)
                              + sizeof(manifest.meta_)
                              + sizeof(manifest.redo_);
  return static_cast<int64_t>(common::ob_crc64(payload, payload_len));
}

int read_file_all_(const char *path, char *buf, const int64_t buf_len, int64_t &read_len)
{
  int ret = OB_SUCCESS;
  int fd = -1;
  read_len = 0;
  if (OB_ISNULL(path) || OB_ISNULL(buf) || buf_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (0 > (fd = ::open(path, O_RDONLY))) {
    ret = OB_IO_ERROR;
  } else {
    while (OB_SUCC(ret) && read_len < buf_len) {
      const ssize_t nread = ::read(fd, buf + read_len, buf_len - read_len);
      if (0 == nread) {
        break;
      } else if (nread < 0) {
        ret = OB_IO_ERROR;
      } else {
        read_len += nread;
      }
    }
    if (OB_SUCC(ret) && read_len != buf_len) {
      ret = OB_INVALID_DATA;
    }
  }
  if (fd >= 0) {
    ::close(fd);
  }
  return ret;
}

int write_file_all_(const char *path, const char *buf, const int64_t buf_len)
{
  int ret = OB_SUCCESS;
  int fd = -1;
  int64_t written = 0;
  if (OB_ISNULL(path) || OB_ISNULL(buf) || buf_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (0 > (fd = ::open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644))) {
    ret = OB_IO_ERROR;
  } else {
    while (OB_SUCC(ret) && written < buf_len) {
      const ssize_t nwrite = ::write(fd, buf + written, buf_len - written);
      if (nwrite < 0) {
        ret = OB_IO_ERROR;
      } else {
        written += nwrite;
      }
    }
    if (OB_SUCC(ret) && 0 != ::fsync(fd)) {
      ret = OB_IO_ERROR;
    }
  }
  if (fd >= 0) {
    ::close(fd);
  }
  return ret;
}
} // namespace

int load_embed_palf_warm_manifest(const char *log_stream_dir, EmbedPalfWarmManifest &manifest)
{
  int ret = OB_SUCCESS;
  char path[common::FileDirectoryUtils::MAX_PATH] = {'\0'};
  manifest.reset();
  if (OB_ISNULL(log_stream_dir)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(build_manifest_path_(log_stream_dir, EMBED_PALF_WARM_MANIFEST_FILE, path, sizeof(path)))) {
  } else {
    int64_t read_len = 0;
    if (OB_FAIL(read_file_all_(path, reinterpret_cast<char *>(&manifest), sizeof(manifest), read_len))) {
      ret = OB_ENTRY_NOT_EXIST;
    } else if (EMBED_PALF_WARM_MANIFEST_MAGIC != manifest.magic_
               || EMBED_PALF_WARM_MANIFEST_VERSION != manifest.version_) {
      ret = OB_INVALID_DATA;
    } else if (calc_manifest_payload_checksum_(manifest) != manifest.payload_checksum_) {
      ret = OB_CHECKSUM_ERROR;
    } else if (!manifest.is_valid()) {
      ret = OB_INVALID_DATA;
    } else {
      PALF_LOG(INFO, "load embed palf warm manifest success", K(path), K(manifest.write_ts_us_));
    }
  }
  return ret;
}

int save_embed_palf_warm_manifest(const char *log_stream_dir, const EmbedPalfWarmManifest &manifest)
{
  int ret = OB_SUCCESS;
  char path[common::FileDirectoryUtils::MAX_PATH] = {'\0'};
  char tmp_path[common::FileDirectoryUtils::MAX_PATH] = {'\0'};
  EmbedPalfWarmManifest to_write = manifest;
  if (OB_ISNULL(log_stream_dir) || !manifest.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(build_manifest_path_(log_stream_dir, EMBED_PALF_WARM_MANIFEST_FILE, path, sizeof(path)))) {
  } else if (OB_FAIL(build_manifest_path_(log_stream_dir, EMBED_PALF_WARM_MANIFEST_TMP_FILE,
                                          tmp_path, sizeof(tmp_path)))) {
  } else {
    to_write.magic_ = EMBED_PALF_WARM_MANIFEST_MAGIC;
    to_write.version_ = EMBED_PALF_WARM_MANIFEST_VERSION;
    if (0 == to_write.write_ts_us_) {
      to_write.write_ts_us_ = common::ObTimeUtility::current_time();
    }
    to_write.payload_checksum_ = calc_manifest_payload_checksum_(to_write);
    if (OB_FAIL(write_file_all_(tmp_path, reinterpret_cast<const char *>(&to_write), sizeof(to_write)))) {
    } else if (0 != ::rename(tmp_path, path)) {
      ret = OB_IO_ERROR;
    } else if (OB_FAIL(common::FileDirectoryUtils::fsync_dir(log_stream_dir))) {
    } else {
      PALF_LOG(INFO, "save embed palf warm manifest success", K(path), K(to_write.write_ts_us_));
    }
    if (OB_FAIL(ret)) {
      (void)common::FileDirectoryUtils::delete_file(tmp_path);
    }
  }
  return ret;
}

int delete_embed_palf_warm_manifest(const char *log_stream_dir)
{
  int ret = OB_SUCCESS;
  char path[common::FileDirectoryUtils::MAX_PATH] = {'\0'};
  bool exists = false;
  if (OB_ISNULL(log_stream_dir)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(build_manifest_path_(log_stream_dir, EMBED_PALF_WARM_MANIFEST_FILE, path, sizeof(path)))) {
  } else if (OB_FAIL(common::FileDirectoryUtils::is_exists(path, exists))) {
  } else if (!exists) {
  } else if (OB_FAIL(common::FileDirectoryUtils::delete_file(path))) {
  } else {
    PALF_LOG(INFO, "delete embed palf warm manifest success", K(path));
  }
  return ret;
}

#else  // _WIN32

int load_embed_palf_warm_manifest(const char *log_stream_dir, EmbedPalfWarmManifest &manifest)
{
  manifest.reset();
  return OB_ENTRY_NOT_EXIST;
}

int save_embed_palf_warm_manifest(const char *log_stream_dir, const EmbedPalfWarmManifest &manifest)
{
  (void)log_stream_dir;
  (void)manifest;
  return OB_SUCCESS;
}

int delete_embed_palf_warm_manifest(const char *log_stream_dir)
{
  (void)log_stream_dir;
  return OB_SUCCESS;
}

#endif  // _WIN32

} // namespace palf
} // namespace oceanbase

#endif // OB_BUILD_EMBED_MODE
