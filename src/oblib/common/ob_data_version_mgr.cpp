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

#define USING_LOG_PREFIX COMMON

#include "common/ob_data_version_mgr.h"
#include "common/ob_record_header.h"

#define DV_ILOG_F(fmt, args...) COMMON_LOG(INFO, "[DATA_VERSION] " fmt, ##args)

namespace oceanbase
{
namespace common
{

ObDataVersionMgr& ObDataVersionMgr::get_instance()
{
  static ObDataVersionMgr mgr;
  return mgr;
}

int ObDataVersionMgr::init()
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
  } else {
    version_ = nullptr;
    is_inited_ = true;
    file_exists_when_loading_ = false;
  }

  return ret;
}

int ObDataVersionMgr::validate_or_init_current_version()
{
  int ret = OB_SUCCESS;
  SpinWLockGuard guard(lock_);

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "ObDataVersionMgr doesn't init", K(ret));
  } else if (OB_ISNULL(version_)) {
    if (OB_FAIL(init_current_version_())) {
      COMMON_LOG(WARN, "failed to initialize current data version", K(ret));
    }
  } else if (DATA_CURRENT_VERSION != version_->get_version()) {
    ret = OB_NOT_SUPPORTED;
    COMMON_LOG(ERROR, "persisted data version does not match this binary",
               K(ret), "persisted_data_version", DVP(version_->get_version()),
               "binary_data_version", DVP(DATA_CURRENT_VERSION));
  } else {
    DV_ILOG_F("persisted data version matches this binary", K(ret), KPC(version_));
  }

  return ret;
}

int ObDataVersionMgr::load_from_file()
{
  int ret = OB_SUCCESS;
  int fd = 0;
  const char *file_path = DATA_VERSION_FILE_PATH;
  SpinWLockGuard guard(lock_);

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "ObDataVersionMgr doesn't init", K(ret));
#ifdef _WIN32
  } else if ((fd = ::open(file_path, O_RDONLY | _O_BINARY)) < 0) {
#else
  } else if ((fd = ::open(file_path, O_RDONLY)) < 0) {
#endif
    if (ENOENT != errno) {
      ret = OB_IO_ERROR;
      COMMON_LOG(WARN, "fail to open data_version file", K(ret), K(errno), K(file_path));
    } else {
      // when errno is ENOENT, the file does not exist
      COMMON_LOG(WARN, "data_version file doesn't exist, skip load");
    }
  } else {
    char *load_buf = NULL;
    int64_t buf_size = DATA_VERSION_FILE_MAX_SIZE;
    PageArena<> pa;
    set_file_exists_when_loading_();

    if (OB_ISNULL(load_buf = pa.alloc(buf_size))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      COMMON_LOG(ERROR, "fail to alloc buf", K(ret), K(buf_size));
    } else {
      MEMSET(load_buf, 0, buf_size);
      ssize_t read_len = ::read(fd, load_buf, buf_size);
      if (read_len < 0) {
        ret = OB_IO_ERROR;
        COMMON_LOG(WARN, "fail to read data_version file", K(ret), K(read_len), K(errno), K(fd));
      } else {
        // deserialize header
        // checksum check
        // load data_version
        ObRecordHeader header;
        int64_t pos = 0;
         if (OB_FAIL(header.deserialize(load_buf, read_len, pos))) {
          COMMON_LOG(ERROR, "deserialize header failed", K(ret), K(read_len), K(pos));
        } else {
          const int64_t header_length = header.header_length_;
          const int64_t data_length = read_len - header_length;
          const char *const p_data = load_buf + header_length;
          if (data_length <= 0 || data_length != header.data_zlength_) {
            ret = OB_INVALID_DATA;
            COMMON_LOG(ERROR, "invalid data length", K(ret), K(header_length),
                       K(data_length), K(buf_size), K(read_len), K(header));
          } else if (OB_FAIL(header.check_header_checksum())) {
            COMMON_LOG(ERROR, "check header checksum failed", K(ret), K(header));
          } else if (OB_CONFIG_MAGIC != header.magic_) {
            ret = OB_INVALID_DATA;
            COMMON_LOG(ERROR, "check magic number failed", K(ret),
                       K_(header.magic));
          } else if (OB_CONFIG_VERSION != header.version_) {
            ret = OB_NOT_SUPPORTED;
            COMMON_LOG(ERROR, "persisted data-version file format is not supported",
                       K(ret), "persisted_format", header.version_,
                       "current_format", OB_CONFIG_VERSION);
          } else if ('\n' != p_data[data_length - 1]) {
            ret = OB_INVALID_DATA;
            COMMON_LOG(ERROR, "data-version payload is incomplete", K(ret), K(data_length));
          } else if (OB_FAIL(header.check_payload_checksum(p_data, data_length))) {
            COMMON_LOG(ERROR, "check data checksum failed", K(ret));
          } else {
            while (OB_SUCC(ret) && pos < read_len) {
              ret = load_data_version_(load_buf, pos);
            }
            if (OB_ITER_END == ret) {
              ret = OB_SUCCESS;
            }
          }
        }
      }
    }
    if (0 != close(fd)) {
      if (OB_SUCC(ret)) {
        ret = OB_IO_ERROR;
        COMMON_LOG(WARN, "fail to close data_version file fd", K(ret), K(errno), K(fd));
      }
    }
  }

  COMMON_LOG(INFO, "[DATA_VERSION] load data_version file", K(ret), KP(version_));

  return ret;
}

int ObDataVersionMgr::init_current_version_()
{
  int ret = OB_SUCCESS;
  void *version_buf = NULL;
  if (OB_NOT_NULL(version_)) {
    ret = OB_INIT_TWICE;
    COMMON_LOG(WARN, "data version has already been initialized", K(ret), KPC(version_));
  } else if (OB_FAIL(dump_current_version_to_file_())) {
    COMMON_LOG(WARN, "failed to persist current data version", K(ret));
  } else if (OB_ISNULL(version_buf = allocator_.alloc(sizeof(ObDataVersion)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    COMMON_LOG(ERROR, "failed to allocate data version", K(ret), K(sizeof(ObDataVersion)));
  } else {
    version_ = new (version_buf) ObDataVersion(DATA_CURRENT_VERSION);
    DV_ILOG_F("initialized persisted data version", K(ret), KPC(version_));
  }

  return ret;
}

int ObDataVersionMgr::dump_current_version_to_file_()
{
  int ret = OB_SUCCESS;
  ObRecordHeader header;
  int64_t header_length = header.get_serialize_size();
  char *dump_buf = NULL;
  int64_t buf_length = header_length + ObDataVersion::MAX_DUMP_BUF_SIZE;
  PageArena<> pa;

  if (OB_ISNULL(dump_buf = pa.alloc(buf_length))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    COMMON_LOG(ERROR, "fail to alloc buf", K(ret), K(buf_length));
  } else {
    MEMSET(dump_buf, 0, buf_length);
    int64_t pos = 0;
    const int64_t data_pos = pos + header_length;
    pos += header_length;
    if (OB_FAIL(dump_data_version_(dump_buf, buf_length, pos,
                                   DATA_CURRENT_VERSION))) {
      COMMON_LOG(WARN, "fail to dump data_version", K(ret), KDV(DATA_CURRENT_VERSION));
    }
    if (OB_FAIL(ret)) {

    } else if (OB_FAIL(write_to_file_(dump_buf, buf_length, pos - data_pos))) {
      COMMON_LOG(WARN, "fail to write data_version file", K(ret));
    }
  }

  return ret;
}

int ObDataVersionMgr::dump_data_version_(char *buf, int64_t buf_length, int64_t &pos,
                                               const uint64_t data_version) {
  int ret = OB_SUCCESS;
  char version_str[OB_SERVER_VERSION_LENGTH]{0};
  if (OB_INVALID_INDEX ==
      VersionUtil::print_version_str(version_str, OB_SERVER_VERSION_LENGTH, data_version)) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "fail to print data_version str", K(ret), KDV(data_version));
  } else if (OB_FAIL(databuff_printf(
                 buf, buf_length, pos, ObDataVersion::DUMP_BUF_FORMAT,
                 version_str, data_version))) {
    COMMON_LOG(WARN, "fail to printf", K(ret), K(buf_length), K(pos));
  } else if (pos >= buf_length) {
    ret = OB_SIZE_OVERFLOW;
    COMMON_LOG(WARN, "buffer size overflow", K(ret), K(buf_length), K(pos));
  } else {
    // The trailing newline makes truncated files distinguishable from valid files.
    buf[pos] = '\n';
    pos += 1;
  }

  return ret;
}

int ObDataVersionMgr::load_data_version_(char *buf, int64_t &pos) {
  int ret = OB_SUCCESS;
  ObDataVersion *version = NULL;
  uint64_t version_val = 0;
  char version_str[OB_SERVER_VERSION_LENGTH]{0};
  char canonical_version_str[OB_SERVER_VERSION_LENGTH]{0};
  char trailing = '\0';
  int res = 0;
  const int expected_item_size = 2;
  char *saveptr = NULL;
  char *token = NULL;

  if (NULL == buf) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "buf is null", K(ret), K(pos));
  } else if (NULL == (token = STRTOK_R(buf + pos, "\n", &saveptr))) {
    ret = OB_ITER_END;
  } else {
    res = sscanf(token, ObDataVersion::LOAD_BUF_FORMAT,
                 version_str, &version_val, &trailing);
    if (res != expected_item_size) {
      ret = OB_INVALID_DATA;
      COMMON_LOG(ERROR, "fail to parse data_version", K(ret), K(res), K(pos),
                 K(token), K(version_val), K(version_str));
    } else if (OB_INVALID_INDEX == VersionUtil::print_version_str(
                   canonical_version_str, OB_SERVER_VERSION_LENGTH, version_val)
               || 0 != STRCMP(canonical_version_str, version_str)) {
      ret = OB_INVALID_DATA;
      COMMON_LOG(ERROR, "data-version string and value do not match", K(ret),
                 K(version_str), K(canonical_version_str), K(version_val));
    } else if (OB_NOT_NULL(version_)) {
      ret = OB_INVALID_DATA;
      COMMON_LOG(ERROR, "data-version file contains more than one entry", K(ret), K(pos));
    } else {
      COMMON_LOG(INFO, "[DATA_VERSION] successfully parse data_version",
                 K(version_val), K(version_str), K(pos));
      void *version_buf = NULL;
      if (OB_ISNULL(version_buf =
                        allocator_.alloc(sizeof(ObDataVersion)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        COMMON_LOG(ERROR, "fail to alloc buf", K(ret), K(sizeof(ObDataVersion)));
      } else if (FALSE_IT(version = new (version_buf) ObDataVersion(version_val))) {

      } else {
        version_ = version;
        pos += (saveptr - token);
      }
    }
  }
  return ret;
}

int ObDataVersionMgr::write_to_file_(char *buf, int64_t buf_length, int64_t data_length)
{
  int ret = OB_SUCCESS;
  int fd = 0;
  ObRecordHeader header;
  const int64_t header_length = header.get_serialize_size();
  const int64_t total_length = header_length + data_length;
  const int64_t max_length = DATA_VERSION_FILE_MAX_SIZE;
  int64_t header_pos = 0;

  if (total_length > buf_length || total_length > max_length) {
    ret = OB_INVALID_DATA;
    COMMON_LOG(WARN, "dump buffer overflow", K(ret), K(total_length),
               K(data_length), K(buf_length), K(max_length));
  } else {
    header.magic_ = OB_CONFIG_MAGIC;
    header.header_length_ = static_cast<int16_t>(header_length);
    header.version_ = OB_CONFIG_VERSION;
    header.data_length_ = static_cast<int32_t>(data_length);
    header.data_zlength_ = header.data_length_;
    header.data_checksum_ = ob_crc64(buf + header_length, data_length);
    header.set_header_checksum();
    if (OB_FAIL(header.serialize(buf, buf_length, header_pos))) {
      COMMON_LOG(WARN, "fail to serialize header", K(ret), K(header), K(buf_length), K(header_pos));
    } else {
      const char *file_path = DATA_VERSION_FILE_PATH;
      char tmp_path[MAX_PATH_SIZE]{0};
      char hist_path[MAX_PATH_SIZE]{0};
      if (OB_FAIL(databuff_printf(tmp_path, MAX_PATH_SIZE, "%s.tmp", file_path))) {
        COMMON_LOG(WARN, "fail to printf", K(ret));
      } else if (OB_FAIL(databuff_printf(hist_path, MAX_PATH_SIZE, "%s.history", file_path))) {
        COMMON_LOG(WARN, "fail to printf", K(ret));
#ifdef _WIN32
    } else if ((fd = ::open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC | _O_BINARY,
                            S_IRUSR | S_IWUSR | S_IRGRP)) < 0) {
#else
    } else if ((fd = ::open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC,
                            S_IRUSR | S_IWUSR | S_IRGRP)) < 0) {
#endif
        ret = OB_IO_ERROR;
        COMMON_LOG(WARN, "fail to open data_version file", K(ret), K(errno),
                  K(fd), K(total_length), K(tmp_path));
      } else if (total_length != ::write(fd, buf, total_length)) {
        ret = OB_IO_ERROR;
        COMMON_LOG(WARN, "fail to write data_version file", K(ret), K(errno),
                  K(fd), K(total_length));
        if (0 != ::close(fd)) {
          COMMON_LOG(WARN, "fail to close data_version file fd", K(ret), K(errno),
                    K(fd), K(total_length));
        }
      } else if (0 != ::fsync(fd)) {
        ret = OB_IO_ERROR;
        COMMON_LOG(WARN, "fail to sync data_version file", K(ret), K(errno),
                  K(fd), K(total_length));
        if (0 != ::close(fd)) {
          COMMON_LOG(WARN, "fail to close data_version file fd", K(ret), K(errno),
                    K(fd), K(total_length));
        }
      } else if (0 != ::close(fd)) {
        ret = OB_IO_ERROR;
        COMMON_LOG(WARN, "fail to close data_version file fd", K(ret), K(errno),
                  K(fd), K(total_length));
      }
      if (OB_SUCC(ret)) {
        if (0 != ::rename(file_path, hist_path) && errno != ENOENT) {
          // it's OK to continue if we fail to backup history file, so we ignore the err ret here
          COMMON_LOG(ERROR, "fail to backup history config file", KERRMSG, K(ret));
        }
        // When running to here, a power outage may occur, resulting in no conf file, requiring the DBA to manually copy the tmp file here
        if (0 != ::rename(tmp_path, file_path)) {
          ret = OB_ERR_SYS;
          COMMON_LOG(WARN, "fail to move tmp config file", KERRMSG, K(ret));
        }
      }
    }

    COMMON_LOG(INFO, "[DATA_VERSION] write data_version file", K(ret),
              K(header_length), K(data_length), K(total_length));
  }

  return ret;
}

} // namespace common
} // namespace oceanbase
