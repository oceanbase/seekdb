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

#include "lib/utility/utility.h"
#include "lib/alloc/alloc_func.h"
#include "lib/time/ob_time_utility.h"
#include "lib/string/ob_sql_string.h"
#include "lib/cpu/ob_cpu_topology.h"
#include "share/config/ob_server_config.h"
#include "share/ob_encryption_util.h"
#include "share/ob_telemetry.h"
#include "common/ob_version_def.h"
#include <curl/curl.h>
#include <errno.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#ifdef ERROR
#undef ERROR
#endif
#else
#include <unistd.h>
#ifdef __APPLE__
#include <time.h>
#include <uuid/uuid.h>
#endif
#endif

#define USING_LOG_PREFIX SHARE

namespace oceanbase
{
namespace share
{

static const char *TELEMETRY_URL = "https://openwebapi.oceanbase.com/api/web/oceanbase/report";
static const char *TELEMETRY_FILE_NAME = "run/telemetry.json";
static const char *TELEMETRY_INSTANCE_ID_ENV_NAME = "SEEKDB_TELEMETRY_INSTANCE_ID";
// v5 replaces the v4 base-directory marker with an optional deterministic
// container scope. Bare-metal and VM installations retain the v3 UUID value.
static const int64_t TELEMETRY_VERSION = 5;
static const int64_t TELEMETRY_MACHINE_ID_BYTE_LENGTH = 16;
static const int64_t TELEMETRY_MACHINE_ID_HEX_LENGTH = 2 * TELEMETRY_MACHINE_ID_BYTE_LENGTH;
static const int64_t TELEMETRY_BASE_DIR_LENGTH_FIELD_SIZE = 8;
static const int64_t TELEMETRY_SCOPE_ID_LENGTH_FIELD_SIZE = 8;
#if defined(__linux__) || defined(__ANDROID__)
static const int64_t TELEMETRY_CONTAINER_ID_MAX_LENGTH = 512;
static const unsigned char TELEMETRY_CONTAINER_RUNTIME_ID_SOURCE = 1;
static const unsigned char TELEMETRY_CONTAINER_HOSTNAME_SOURCE = 2;
static const char TELEMETRY_CONTAINER_SCOPE_DOMAIN[] = "seekdb.telemetry.container-scope.v1";
#endif

// A fixed, product-specific application ID
// (ca528808-4e84-4520-89ce-4845da7d15de). Never change this value: doing so
// would produce a different telemetry UUID for every existing installation.
static const unsigned char TELEMETRY_APP_ID[TELEMETRY_MACHINE_ID_BYTE_LENGTH] = {
  0xca, 0x52, 0x88, 0x08, 0x4e, 0x84, 0x45, 0x20,
  0x89, 0xce, 0x48, 0x45, 0xda, 0x7d, 0x15, 0xde
};

static bool get_telemetry_hex_value(const char ch, unsigned char &value)
{
  bool bret = true;
  if (ch >= '0' && ch <= '9') {
    value = static_cast<unsigned char>(ch - '0');
  } else if (ch >= 'a' && ch <= 'f') {
    value = static_cast<unsigned char>(ch - 'a' + 10);
  } else if (ch >= 'A' && ch <= 'F') {
    value = static_cast<unsigned char>(ch - 'A' + 10);
  } else {
    bret = false;
  }
  return bret;
}

static bool is_telemetry_space(const char ch)
{
  return ' ' == ch || '\t' == ch || '\r' == ch || '\n' == ch;
}

static int parse_telemetry_uuid_text(const char *id,
                                     const int64_t id_len,
                                     unsigned char *id_bytes,
                                     const int64_t id_bytes_len)
{
  int ret = OB_SUCCESS;
  int64_t hex_pos = 0;
  int64_t begin = 0;
  int64_t end = id_len;
  bool all_zero = true;
  bool all_f = true;
  if (OB_ISNULL(id) || id_len <= 0
      || OB_ISNULL(id_bytes)
      || id_bytes_len < TELEMETRY_MACHINE_ID_BYTE_LENGTH) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    MEMSET(id_bytes, 0, id_bytes_len);
    while (begin < end && is_telemetry_space(id[begin])) {
      ++begin;
    }
    while (end > begin && is_telemetry_space(id[end - 1])) {
      --end;
    }
    if (begin < end && ('{' == id[begin] || '}' == id[end - 1])) {
      if ('{' != id[begin] || '}' != id[end - 1]) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        ++begin;
        --end;
      }
    }
    const int64_t text_len = end - begin;
    const bool is_canonical_uuid = TELEMETRY_UUID_STRING_LENGTH == text_len;
    if (OB_SUCC(ret) && TELEMETRY_MACHINE_ID_HEX_LENGTH != text_len && !is_canonical_uuid) {
      ret = OB_INVALID_ARGUMENT;
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < text_len; ++i) {
      const char ch = id[begin + i];
      const bool expect_hyphen = is_canonical_uuid && (8 == i || 13 == i || 18 == i || 23 == i);
      if (expect_hyphen) {
        if ('-' != ch) {
          ret = OB_INVALID_ARGUMENT;
        }
      } else if ('-' == ch) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        unsigned char value = 0;
        if (hex_pos >= TELEMETRY_MACHINE_ID_HEX_LENGTH || !get_telemetry_hex_value(ch, value)) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          const int64_t byte_pos = hex_pos / 2;
          if (0 == hex_pos % 2) {
            id_bytes[byte_pos] = static_cast<unsigned char>(value << 4);
          } else {
            id_bytes[byte_pos] = static_cast<unsigned char>(id_bytes[byte_pos] | value);
          }
          all_zero = all_zero && (0 == value);
          all_f = all_f && (0x0f == value);
          ++hex_pos;
        }
      }
    }
    if (OB_SUCC(ret) && (TELEMETRY_MACHINE_ID_HEX_LENGTH != hex_pos || all_zero || all_f)) {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  return ret;
}

static int format_telemetry_uuid(const unsigned char *uuid_bytes,
                                 char *uuid,
                                 const int64_t uuid_len)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  static const char *HEX_CHARS = "0123456789abcdef";
  if (OB_ISNULL(uuid_bytes) || OB_ISNULL(uuid)
      || uuid_len <= TELEMETRY_UUID_STRING_LENGTH) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    for (int64_t i = 0; i < TELEMETRY_MACHINE_ID_BYTE_LENGTH; ++i) {
      if (4 == i || 6 == i || 8 == i || 10 == i) {
        uuid[pos++] = '-';
      }
      uuid[pos++] = HEX_CHARS[(uuid_bytes[i] >> 4) & 0x0f];
      uuid[pos++] = HEX_CHARS[uuid_bytes[i] & 0x0f];
    }
    uuid[pos] = '\0';
  }
  return ret;
}

static bool is_telemetry_absolute_path(const char *path, const int64_t path_len)
{
#ifdef _WIN32
  const bool has_drive = path_len >= 3
                         && (('a' <= path[0] && 'z' >= path[0])
                             || ('A' <= path[0] && 'Z' >= path[0]))
                         && ':' == path[1]
                         && ('/' == path[2] || '\\' == path[2]);
  bool is_unc = false;
  if (path_len >= 5
      && ('/' == path[0] || '\\' == path[0])
      && ('/' == path[1] || '\\' == path[1])) {
    int64_t server_end = 2;
    while (server_end < path_len && '/' != path[server_end] && '\\' != path[server_end]) {
      ++server_end;
    }
    if (server_end > 2 && server_end + 1 < path_len) {
      int64_t share_end = server_end + 1;
      while (share_end < path_len && '/' != path[share_end] && '\\' != path[share_end]) {
        ++share_end;
      }
      is_unc = share_end > server_end + 1;
    }
  }
  return has_drive || is_unc;
#else
  return path_len > 0 && '/' == path[0];
#endif
}

static int normalize_telemetry_base_dir(const char *base_dir,
                                        const int64_t base_dir_len,
                                        char *normalized_base_dir,
                                        const int64_t normalized_base_dir_size,
                                        int64_t &normalized_base_dir_len)
{
  int ret = OB_SUCCESS;
  normalized_base_dir_len = 0;
  if (OB_ISNULL(base_dir) || base_dir_len <= 0
      || OB_ISNULL(normalized_base_dir) || normalized_base_dir_size <= 1
      || base_dir_len >= normalized_base_dir_size
      || OB_NOT_NULL(memchr(base_dir, '\0', static_cast<size_t>(base_dir_len)))
      || !is_telemetry_absolute_path(base_dir, base_dir_len)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    for (int64_t i = 0; i < base_dir_len; ++i) {
#ifdef _WIN32
      normalized_base_dir[i] = '\\' == base_dir[i] ? '/' : base_dir[i];
#else
      normalized_base_dir[i] = base_dir[i];
#endif
    }
    normalized_base_dir_len = base_dir_len;
#ifdef _WIN32
    // GetFinalPathNameByHandleW returns //?/C:/... or //?/UNC/server/share/....
    // Store the canonical UUID input as C:/... or //server/share/... so direct
    // callers and runtime discovery use the same representation.
    const bool is_extended_unc = normalized_base_dir_len >= 8
                                 && 0 == strncmp(normalized_base_dir, "//?/", 4)
                                 && ('U' == normalized_base_dir[4] || 'u' == normalized_base_dir[4])
                                 && ('N' == normalized_base_dir[5] || 'n' == normalized_base_dir[5])
                                 && ('C' == normalized_base_dir[6] || 'c' == normalized_base_dir[6])
                                 && '/' == normalized_base_dir[7];
    if (is_extended_unc) {
      memmove(normalized_base_dir + 2, normalized_base_dir + 8,
              static_cast<size_t>(normalized_base_dir_len - 8));
      normalized_base_dir_len -= 6;
      normalized_base_dir[0] = '/';
      normalized_base_dir[1] = '/';
    } else if (normalized_base_dir_len >= 4
               && 0 == strncmp(normalized_base_dir, "//?/", 4)) {
      const bool is_extended_drive = normalized_base_dir_len >= 7
                                     && (('a' <= normalized_base_dir[4]
                                          && 'z' >= normalized_base_dir[4])
                                         || ('A' <= normalized_base_dir[4]
                                             && 'Z' >= normalized_base_dir[4]))
                                     && ':' == normalized_base_dir[5]
                                     && '/' == normalized_base_dir[6];
      // Only drive paths drop the extended prefix. GUID/other namespace paths
      // retain //?/ so they cannot collide with a similarly named UNC server.
      if (is_extended_drive) {
        memmove(normalized_base_dir, normalized_base_dir + 4,
                static_cast<size_t>(normalized_base_dir_len - 4));
        normalized_base_dir_len -= 4;
      }
    }
    if (normalized_base_dir_len >= 2
        && 'a' <= normalized_base_dir[0] && 'z' >= normalized_base_dir[0]
        && ':' == normalized_base_dir[1]) {
      normalized_base_dir[0] = static_cast<char>(normalized_base_dir[0] - 'a' + 'A');
    }
#endif
    // The runtime canonicalizer already removes trailing separators. Keep this
    // normalization here as a guard for direct callers of the pure helper.
    while (normalized_base_dir_len > 1
           && '/' == normalized_base_dir[normalized_base_dir_len - 1]) {
#ifdef _WIN32
      if (3 == normalized_base_dir_len && ':' == normalized_base_dir[1]) {
        break;
      }
#endif
      --normalized_base_dir_len;
    }
    normalized_base_dir[normalized_base_dir_len] = '\0';
  }
  return ret;
}

int generate_telemetry_uuid(const char *machine_id,
                            const int64_t machine_id_len,
                            const char *base_dir,
                            const int64_t base_dir_len,
                            const char *scope_id,
                            const int64_t scope_id_len,
                            char *uuid,
                            const int64_t uuid_len)
{
  int ret = OB_SUCCESS;
  unsigned int digest_len = 0;
  const bool has_scope_id = OB_NOT_NULL(scope_id) && scope_id_len > 0;
  const bool valid_scope_args = (OB_ISNULL(scope_id) && 0 == scope_id_len) || has_scope_id;
  unsigned char machine_id_bytes[TELEMETRY_MACHINE_ID_BYTE_LENGTH] = {0};
  unsigned char scope_id_bytes[TELEMETRY_MACHINE_ID_BYTE_LENGTH] = {0};
  char normalized_base_dir[common::OB_MAX_FILE_NAME_LENGTH] = {'\0'};
  int64_t normalized_base_dir_len = 0;
  unsigned char hmac_input[sizeof(TELEMETRY_APP_ID)
                           + TELEMETRY_BASE_DIR_LENGTH_FIELD_SIZE
                           + common::OB_MAX_FILE_NAME_LENGTH
                           + TELEMETRY_SCOPE_ID_LENGTH_FIELD_SIZE
                           + TELEMETRY_MACHINE_ID_BYTE_LENGTH] = {0};
  unsigned char digest[SHA256_DIGEST_LENGTH] = {0};
  unsigned char uuid_bytes[TELEMETRY_MACHINE_ID_BYTE_LENGTH] = {0};
  if (OB_ISNULL(uuid) || uuid_len <= TELEMETRY_UUID_STRING_LENGTH || !valid_scope_args) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    uuid[0] = '\0';
    if (OB_FAIL(parse_telemetry_uuid_text(machine_id, machine_id_len,
                                          machine_id_bytes, sizeof(machine_id_bytes)))) {
    } else if (OB_FAIL(normalize_telemetry_base_dir(
        base_dir, base_dir_len, normalized_base_dir, sizeof(normalized_base_dir),
        normalized_base_dir_len))) {
    } else if (has_scope_id
               && OB_FAIL(parse_telemetry_uuid_text(
                   scope_id, scope_id_len, scope_id_bytes, sizeof(scope_id_bytes)))) {
      LOG_WARN("Invalid container scope ID for telemetry UUID", K(ret), K(scope_id_len));
    } else {
      int64_t input_pos = 0;
      // Freeze the derivation layout as:
      // app-id[16] || uint64_be(base-dir byte length) || canonical base-dir bytes
      // [|| uint64_be(16) || container-scope-id bytes[16]]. The optional suffix
      // leaves non-container installations compatible with telemetry v3.
      MEMCPY(hmac_input + input_pos, TELEMETRY_APP_ID, sizeof(TELEMETRY_APP_ID));
      input_pos += sizeof(TELEMETRY_APP_ID);
      const uint64_t path_len = static_cast<uint64_t>(normalized_base_dir_len);
      for (int64_t i = 0; i < TELEMETRY_BASE_DIR_LENGTH_FIELD_SIZE; ++i) {
        hmac_input[input_pos + i] = static_cast<unsigned char>(
            path_len >> (8 * (TELEMETRY_BASE_DIR_LENGTH_FIELD_SIZE - i - 1)));
      }
      input_pos += TELEMETRY_BASE_DIR_LENGTH_FIELD_SIZE;
      MEMCPY(hmac_input + input_pos, normalized_base_dir, normalized_base_dir_len);
      input_pos += normalized_base_dir_len;
      if (has_scope_id) {
        const uint64_t scope_len = sizeof(scope_id_bytes);
        for (int64_t i = 0; i < TELEMETRY_SCOPE_ID_LENGTH_FIELD_SIZE; ++i) {
          hmac_input[input_pos + i] = static_cast<unsigned char>(
              scope_len >> (8 * (TELEMETRY_SCOPE_ID_LENGTH_FIELD_SIZE - i - 1)));
        }
        input_pos += TELEMETRY_SCOPE_ID_LENGTH_FIELD_SIZE;
        MEMCPY(hmac_input + input_pos, scope_id_bytes, sizeof(scope_id_bytes));
        input_pos += sizeof(scope_id_bytes);
      }

      if (OB_ISNULL(HMAC(EVP_sha256(),
                         machine_id_bytes, static_cast<int>(sizeof(machine_id_bytes)),
                         hmac_input, static_cast<size_t>(input_pos),
                         digest, &digest_len))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Failed to generate telemetry UUID digest", K(ret));
      } else if (OB_UNLIKELY(SHA256_DIGEST_LENGTH != digest_len)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Unexpected telemetry UUID digest length", K(ret), K(digest_len));
      } else {
        MEMCPY(uuid_bytes, digest, sizeof(uuid_bytes));
        uuid_bytes[6] = static_cast<unsigned char>((uuid_bytes[6] & 0x0f) | 0x80); // UUID v8
        uuid_bytes[8] = static_cast<unsigned char>((uuid_bytes[8] & 0x3f) | 0x80); // RFC variant
        if (OB_FAIL(format_telemetry_uuid(uuid_bytes, uuid, uuid_len))) {
        }
      }
    }
  }
  MEMSET(machine_id_bytes, 0, sizeof(machine_id_bytes));
  MEMSET(scope_id_bytes, 0, sizeof(scope_id_bytes));
  MEMSET(normalized_base_dir, 0, sizeof(normalized_base_dir));
  MEMSET(hmac_input, 0, sizeof(hmac_input));
  MEMSET(digest, 0, sizeof(digest));
  MEMSET(uuid_bytes, 0, sizeof(uuid_bytes));
  return ret;
}

#if defined(__linux__) || defined(__ANDROID__)
static int read_telemetry_uuid_file(const char *file_name,
                                    char *id,
                                    const int64_t id_size,
                                    int64_t &read_len)
{
  int ret = OB_SUCCESS;
  char raw_id[128] = {'\0'};
  unsigned char parsed_id[TELEMETRY_MACHINE_ID_BYTE_LENGTH] = {0};
  FILE *fp = nullptr;
  read_len = 0;
  if (OB_ISNULL(file_name) || OB_ISNULL(id) || id_size <= TELEMETRY_UUID_STRING_LENGTH) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(fp = fopen(file_name, "r"))) {
    ret = ENOENT == errno ? OB_FILE_NOT_EXIST : OB_IO_ERROR;
  } else {
    const size_t size = fread(raw_id, 1, sizeof(raw_id) - 1, fp);
    if (0 != ferror(fp)) {
      ret = OB_IO_ERROR;
    } else if (size == sizeof(raw_id) - 1 && 0 == feof(fp)) {
      ret = OB_SIZE_OVERFLOW;
    } else if (OB_FAIL(parse_telemetry_uuid_text(raw_id, size, parsed_id, sizeof(parsed_id)))) {
      // Reject an existing but malformed identity instead of silently replacing it.
    } else if (OB_FAIL(format_telemetry_uuid(parsed_id, id, id_size))) {
    } else {
      read_len = TELEMETRY_UUID_STRING_LENGTH;
    }
    fclose(fp);
  }
  MEMSET(raw_id, 0, sizeof(raw_id));
  MEMSET(parsed_id, 0, sizeof(parsed_id));
  return ret;
}
#endif

static int normalize_telemetry_uuid_text(const char *raw_id,
                                         const int64_t raw_id_len,
                                         char *id,
                                         const int64_t id_size,
                                         int64_t &id_len)
{
  int ret = OB_SUCCESS;
  unsigned char id_bytes[TELEMETRY_MACHINE_ID_BYTE_LENGTH] = {0};
  id_len = 0;
  if (OB_ISNULL(id) || id_size <= TELEMETRY_UUID_STRING_LENGTH) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(parse_telemetry_uuid_text(raw_id, raw_id_len,
                                               id_bytes, sizeof(id_bytes)))) {
  } else if (OB_FAIL(format_telemetry_uuid(id_bytes, id, id_size))) {
  } else {
    id_len = TELEMETRY_UUID_STRING_LENGTH;
  }
  MEMSET(id_bytes, 0, sizeof(id_bytes));
  return ret;
}

#if defined(__linux__) || defined(__ANDROID__)
static int generate_telemetry_container_scope_id(
    const char *identity,
    const int64_t identity_len,
    const unsigned char identity_source,
    char *scope_id,
    const int64_t scope_id_size,
    int64_t &scope_id_len)
{
  int ret = OB_SUCCESS;
  unsigned int digest_len = 0;
  unsigned char hmac_input[sizeof(TELEMETRY_CONTAINER_SCOPE_DOMAIN) - 1
                           + 1 + sizeof(uint64_t)
                           + TELEMETRY_CONTAINER_ID_MAX_LENGTH] = {0};
  unsigned char digest[SHA256_DIGEST_LENGTH] = {0};
  unsigned char uuid_bytes[TELEMETRY_MACHINE_ID_BYTE_LENGTH] = {0};
  scope_id_len = 0;
  if (OB_ISNULL(identity) || identity_len <= 0
      || identity_len > TELEMETRY_CONTAINER_ID_MAX_LENGTH
      || OB_NOT_NULL(memchr(identity, '\0', static_cast<size_t>(identity_len)))
      || (TELEMETRY_CONTAINER_RUNTIME_ID_SOURCE != identity_source
          && TELEMETRY_CONTAINER_HOSTNAME_SOURCE != identity_source)
      || OB_ISNULL(scope_id) || scope_id_size <= TELEMETRY_UUID_STRING_LENGTH) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    int64_t input_pos = 0;
    // Freeze automatic scope derivation as:
    // HMAC-SHA256(app-id, domain || source || uint64_be(length) || identity).
    MEMCPY(hmac_input + input_pos, TELEMETRY_CONTAINER_SCOPE_DOMAIN,
           sizeof(TELEMETRY_CONTAINER_SCOPE_DOMAIN) - 1);
    input_pos += sizeof(TELEMETRY_CONTAINER_SCOPE_DOMAIN) - 1;
    hmac_input[input_pos++] = identity_source;
    const uint64_t value_len = static_cast<uint64_t>(identity_len);
    for (int64_t i = 0; i < static_cast<int64_t>(sizeof(value_len)); ++i) {
      hmac_input[input_pos + i] = static_cast<unsigned char>(
          value_len >> (8 * (sizeof(value_len) - i - 1)));
    }
    input_pos += sizeof(value_len);
    MEMCPY(hmac_input + input_pos, identity, identity_len);
    input_pos += identity_len;
    if (OB_ISNULL(HMAC(EVP_sha256(),
                       TELEMETRY_APP_ID, static_cast<int>(sizeof(TELEMETRY_APP_ID)),
                       hmac_input, static_cast<size_t>(input_pos), digest, &digest_len))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Failed to generate telemetry container scope digest", K(ret));
    } else if (OB_UNLIKELY(SHA256_DIGEST_LENGTH != digest_len)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Unexpected telemetry container scope digest length", K(ret), K(digest_len));
    } else {
      MEMCPY(uuid_bytes, digest, sizeof(uuid_bytes));
      uuid_bytes[6] = static_cast<unsigned char>((uuid_bytes[6] & 0x0f) | 0x80); // UUID v8
      uuid_bytes[8] = static_cast<unsigned char>((uuid_bytes[8] & 0x3f) | 0x80); // RFC variant
      if (OB_FAIL(format_telemetry_uuid(uuid_bytes, scope_id, scope_id_size))) {
      } else {
        scope_id_len = TELEMETRY_UUID_STRING_LENGTH;
      }
    }
  }
  MEMSET(hmac_input, 0, sizeof(hmac_input));
  MEMSET(digest, 0, sizeof(digest));
  MEMSET(uuid_bytes, 0, sizeof(uuid_bytes));
  return ret;
}

static bool is_telemetry_hex_char(const char ch)
{
  return ('0' <= ch && '9' >= ch)
         || ('a' <= ch && 'f' >= ch)
         || ('A' <= ch && 'F' >= ch);
}

static bool telemetry_text_matches(const char *text,
                                   const int64_t text_len,
                                   const int64_t pos,
                                   const char *pattern)
{
  const int64_t pattern_len = static_cast<int64_t>(strlen(pattern));
  return pos >= 0 && pattern_len <= text_len - pos
         && 0 == memcmp(text + pos, pattern, static_cast<size_t>(pattern_len));
}

static bool find_telemetry_runtime_id(const char *text,
                                      const int64_t text_len,
                                      const bool allow_kubepods_id,
                                      char *runtime_id,
                                      const int64_t runtime_id_size,
                                      int64_t &runtime_id_len,
                                      bool &has_runtime_marker)
{
  bool found = false;
  runtime_id_len = 0;
  has_runtime_marker = false;
  static const char *RUNTIME_ID_PREFIXES[] = {
    "docker-", "/docker/", "containerd-", "cri-containerd-", "crio-",
    "libpod-", "/containers/", "/overlay-containers/"
  };
  static const char *RUNTIME_MARKERS[] = {
    "kubepods", "/lxc/", "lxc.payload"
  };
  if (OB_NOT_NULL(text) && text_len > 0
      && OB_NOT_NULL(runtime_id) && runtime_id_size > 64) {
    for (int64_t prefix_idx = 0;
         !found && prefix_idx < ARRAYSIZEOF(RUNTIME_ID_PREFIXES);
         ++prefix_idx) {
      const char *prefix = RUNTIME_ID_PREFIXES[prefix_idx];
      const int64_t prefix_len = static_cast<int64_t>(strlen(prefix));
      for (int64_t prefix_pos = 0; !found && prefix_pos < text_len; ++prefix_pos) {
        const bool has_component_boundary = '/' == prefix[0]
                                            || 0 == prefix_pos
                                            || '/' == text[prefix_pos - 1]
                                            || ':' == text[prefix_pos - 1];
        if (has_component_boundary
            && telemetry_text_matches(text, text_len, prefix_pos, prefix)) {
          has_runtime_marker = true;
          const int64_t id_begin = prefix_pos + prefix_len;
          const int64_t id_end = id_begin + 64;
          if (id_end <= text_len
              && (id_end == text_len || !is_telemetry_hex_char(text[id_end]))) {
            found = true;
            for (int64_t i = 0; found && i < 64; ++i) {
              const char ch = text[id_begin + i];
              if (!is_telemetry_hex_char(ch)) {
                found = false;
              } else {
                runtime_id[i] = ('A' <= ch && 'F' >= ch)
                                ? static_cast<char>(ch - 'A' + 'a') : ch;
              }
            }
            if (found) {
              runtime_id[64] = '\0';
              runtime_id_len = 64;
            }
          }
        }
      }
    }
    for (int64_t marker_idx = 0;
         marker_idx < ARRAYSIZEOF(RUNTIME_MARKERS);
         ++marker_idx) {
      const char *marker = RUNTIME_MARKERS[marker_idx];
      for (int64_t marker_pos = 0; marker_pos < text_len; ++marker_pos) {
        if (telemetry_text_matches(text, text_len, marker_pos, marker)) {
          has_runtime_marker = true;
          if (!found && allow_kubepods_id && 0 == strcmp(marker, "kubepods")) {
            int64_t line_end = marker_pos;
            while (line_end < text_len && '\n' != text[line_end]
                   && '\r' != text[line_end]) {
              ++line_end;
            }
            int64_t pos = marker_pos;
            while (!found && pos < line_end) {
              if (!is_telemetry_hex_char(text[pos])) {
                ++pos;
              } else {
                const int64_t id_begin = pos;
                while (pos < line_end && is_telemetry_hex_char(text[pos])) {
                  ++pos;
                }
                if (64 == pos - id_begin) {
                  for (int64_t i = 0; i < 64; ++i) {
                    const char ch = text[id_begin + i];
                    runtime_id[i] = ('A' <= ch && 'F' >= ch)
                                    ? static_cast<char>(ch - 'A' + 'a') : ch;
                  }
                  runtime_id[64] = '\0';
                  runtime_id_len = 64;
                  found = true;
                }
              }
            }
          }
        }
      }
    }
  }
  return found;
}

static int read_telemetry_runtime_id_file(const char *file_name,
                                          const bool allow_kubepods_id,
                                          char *runtime_id,
                                          const int64_t runtime_id_size,
                                          int64_t &runtime_id_len,
                                          bool &has_runtime_marker)
{
  int ret = OB_ENTRY_NOT_EXIST;
  char content[4096 + 1024 + 1] = {'\0'};
  int64_t carry_len = 0;
  FILE *fp = nullptr;
  runtime_id_len = 0;
  if (OB_ISNULL(file_name) || OB_ISNULL(runtime_id) || runtime_id_size <= 64) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(fp = fopen(file_name, "r"))) {
    ret = ENOENT == errno ? OB_FILE_NOT_EXIST : OB_IO_ERROR;
  } else {
    bool done = false;
    while (!done) {
      const size_t read_size = fread(content + carry_len, 1, 4096, fp);
      const int64_t content_len = carry_len + static_cast<int64_t>(read_size);
      content[content_len] = '\0';
      bool file_has_marker = false;
      if (find_telemetry_runtime_id(content, content_len, allow_kubepods_id,
                                    runtime_id, runtime_id_size, runtime_id_len,
                                    file_has_marker)) {
        ret = OB_SUCCESS;
        done = true;
      } else {
        has_runtime_marker = has_runtime_marker || file_has_marker;
        if (0 != ferror(fp)) {
          ret = OB_IO_ERROR;
          done = true;
        } else if (0 != feof(fp)) {
          done = true;
        } else if (0 == read_size) {
          ret = OB_IO_ERROR;
          done = true;
        } else {
          carry_len = content_len < 1024 ? content_len : 1024;
          memmove(content, content + content_len - carry_len,
                  static_cast<size_t>(carry_len));
        }
      }
    }
    fclose(fp);
  }
  MEMSET(content, 0, sizeof(content));
  return ret;
}

static int read_telemetry_text_file(const char *file_name,
                                    char *content,
                                    const int64_t content_size,
                                    int64_t &content_len)
{
  int ret = OB_SUCCESS;
  FILE *fp = nullptr;
  content_len = 0;
  if (OB_ISNULL(file_name) || OB_ISNULL(content) || content_size <= 1) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(fp = fopen(file_name, "r"))) {
    ret = ENOENT == errno ? OB_FILE_NOT_EXIST : OB_IO_ERROR;
  } else {
    const size_t read_size = fread(content, 1, static_cast<size_t>(content_size - 1), fp);
    if (0 != ferror(fp)) {
      ret = OB_IO_ERROR;
    } else if (read_size == static_cast<size_t>(content_size - 1) && 0 == feof(fp)) {
      ret = OB_SIZE_OVERFLOW;
    } else {
      content[read_size] = '\0';
      content_len = static_cast<int64_t>(read_size);
    }
    fclose(fp);
  }
  return ret;
}

static int get_telemetry_podman_runtime_id(char *runtime_id,
                                           const int64_t runtime_id_size,
                                           int64_t &runtime_id_len)
{
  int ret = OB_ENTRY_NOT_EXIST;
  char content[4096] = {'\0'};
  int64_t content_len = 0;
  runtime_id_len = 0;
  if (OB_ISNULL(runtime_id) || runtime_id_size <= 64) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const int read_ret = read_telemetry_text_file(
        "/run/.containerenv", content, sizeof(content), content_len);
    if (OB_SUCCESS == read_ret) {
      int64_t line_begin = 0;
      while (OB_ENTRY_NOT_EXIST == ret && line_begin < content_len) {
        int64_t line_end = line_begin;
        while (line_end < content_len && '\n' != content[line_end]
               && '\r' != content[line_end]) {
          ++line_end;
        }
        int64_t begin = line_begin;
        int64_t end = line_end;
        while (begin < end && is_telemetry_space(content[begin])) {
          ++begin;
        }
        while (end > begin && is_telemetry_space(content[end - 1])) {
          --end;
        }
        if (end - begin >= 3 && 'i' == content[begin]
            && 'd' == content[begin + 1] && '=' == content[begin + 2]) {
          begin += 3;
          while (begin < end && is_telemetry_space(content[begin])) {
            ++begin;
          }
          while (end > begin && is_telemetry_space(content[end - 1])) {
            --end;
          }
          if (end - begin >= 2
              && (('\'' == content[begin] && '\'' == content[end - 1])
                  || ('"' == content[begin] && '"' == content[end - 1]))) {
            ++begin;
            --end;
          }
          if (64 != end - begin) {
            ret = OB_INVALID_ARGUMENT;
          } else {
            ret = OB_SUCCESS;
            for (int64_t i = 0; OB_SUCCESS == ret && i < 64; ++i) {
              const char ch = content[begin + i];
              if (!is_telemetry_hex_char(ch)) {
                ret = OB_INVALID_ARGUMENT;
              } else {
                runtime_id[i] = ('A' <= ch && 'F' >= ch)
                                ? static_cast<char>(ch - 'A' + 'a') : ch;
              }
            }
            if (OB_SUCCESS == ret) {
              runtime_id[64] = '\0';
              runtime_id_len = 64;
            }
          }
        }
        line_begin = line_end + 1;
      }
    } else {
      ret = read_ret;
    }
  }
  MEMSET(content, 0, sizeof(content));
  return ret;
}

static bool is_telemetry_container_marker_present()
{
  const char *container_type = getenv("container");
  const char *kubernetes_host = getenv("KUBERNETES_SERVICE_HOST");
  return 0 == access("/.dockerenv", F_OK)
         || 0 == access("/run/.containerenv", F_OK)
         || 0 == access("/run/systemd/container", F_OK)
         || 0 == access("/run/host/container-manager", F_OK)
         || (OB_NOT_NULL(container_type) && '\0' != container_type[0])
         || (OB_NOT_NULL(kubernetes_host) && '\0' != kubernetes_host[0]);
}

static bool normalize_telemetry_default_container_hostname(char *hostname,
                                                           const int64_t hostname_len)
{
  bool valid = OB_NOT_NULL(hostname) && (12 == hostname_len || 64 == hostname_len);
  for (int64_t i = 0; valid && i < hostname_len; ++i) {
    if (!is_telemetry_hex_char(hostname[i])) {
      valid = false;
    } else if ('A' <= hostname[i] && 'F' >= hostname[i]) {
      hostname[i] = static_cast<char>(hostname[i] - 'A' + 'a');
    }
  }
  return valid;
}
#endif

// Every automatic source below lives outside the database base directory, so
// deleting and recreating that directory cannot change the UUID. Runtime IDs
// identify one container object; callers that need identity to survive
// container replacement must inject SEEKDB_TELEMETRY_INSTANCE_ID.
static int get_telemetry_container_scope_id(char *scope_id,
                                            const int64_t scope_id_size,
                                            int64_t &scope_id_len,
                                            bool &has_scope_id)
{
  int ret = OB_SUCCESS;
  const char *configured_id = getenv(TELEMETRY_INSTANCE_ID_ENV_NAME);
  scope_id_len = 0;
  has_scope_id = false;
  if (OB_ISNULL(scope_id) || scope_id_size <= TELEMETRY_UUID_STRING_LENGTH) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_NOT_NULL(configured_id)) {
    const int64_t configured_id_len = static_cast<int64_t>(strlen(configured_id));
    if (OB_FAIL(normalize_telemetry_uuid_text(configured_id, configured_id_len,
                                              scope_id, scope_id_size, scope_id_len))) {
    } else {
      has_scope_id = true;
    }
  } else {
#if defined(__linux__) || defined(__ANDROID__)
    const int systemd_file_ret = read_telemetry_uuid_file(
        "/run/host/container-uuid", scope_id, scope_id_size, scope_id_len);
    if (OB_SUCCESS == systemd_file_ret) {
      has_scope_id = true;
    } else {
      if (OB_FILE_NOT_EXIST != systemd_file_ret) {
        LOG_WARN("Ignoring unavailable systemd container UUID file", K(systemd_file_ret));
      }
      const char *systemd_container_uuid = getenv("container_uuid");
      if (OB_NOT_NULL(systemd_container_uuid)) {
        const int64_t value_len = static_cast<int64_t>(strlen(systemd_container_uuid));
        const int normalize_ret = normalize_telemetry_uuid_text(
            systemd_container_uuid, value_len, scope_id, scope_id_size, scope_id_len);
        if (OB_SUCCESS == normalize_ret) {
          has_scope_id = true;
        } else {
          LOG_WARN("Ignoring invalid systemd container UUID", K(normalize_ret), K(value_len));
        }
      }
    }
    bool container_detected = is_telemetry_container_marker_present();
    char runtime_id[65] = {'\0'};
    int64_t runtime_id_len = 0;
    if (!has_scope_id) {
      const int podman_ret = get_telemetry_podman_runtime_id(
          runtime_id, sizeof(runtime_id), runtime_id_len);
      if (OB_SUCCESS == podman_ret) {
        container_detected = true;
        ret = generate_telemetry_container_scope_id(
            runtime_id, runtime_id_len, TELEMETRY_CONTAINER_RUNTIME_ID_SOURCE,
            scope_id, scope_id_size, scope_id_len);
        has_scope_id = OB_SUCCESS == ret;
      } else if (OB_FILE_NOT_EXIST != podman_ret && OB_ENTRY_NOT_EXIST != podman_ret) {
        LOG_WARN("Ignoring unavailable Podman container ID", K(podman_ret));
      }
    }
    static const char *CGROUP_ID_FILES[] = {
      "/proc/self/cgroup", "/proc/self/cpuset"
    };
    for (int64_t i = 0; OB_SUCC(ret) && !has_scope_id
         && i < ARRAYSIZEOF(CGROUP_ID_FILES); ++i) {
      bool has_runtime_marker = false;
      const int read_ret = read_telemetry_runtime_id_file(
          CGROUP_ID_FILES[i], true, runtime_id, sizeof(runtime_id), runtime_id_len,
          has_runtime_marker);
      container_detected = container_detected || has_runtime_marker;
      if (OB_SUCCESS == read_ret) {
        if (OB_FAIL(generate_telemetry_container_scope_id(
            runtime_id, runtime_id_len, TELEMETRY_CONTAINER_RUNTIME_ID_SOURCE,
            scope_id, scope_id_size, scope_id_len))) {
        } else {
          has_scope_id = true;
        }
      }
    }
    static const char *MOUNT_INFO_FILES[] = {"/proc/self/mountinfo"};
    for (int64_t i = 0; OB_SUCC(ret) && container_detected && !has_scope_id
         && i < ARRAYSIZEOF(MOUNT_INFO_FILES); ++i) {
      bool has_runtime_marker = false;
      const int read_ret = read_telemetry_runtime_id_file(
          MOUNT_INFO_FILES[i], false, runtime_id, sizeof(runtime_id), runtime_id_len,
          has_runtime_marker);
      if (OB_SUCCESS == read_ret) {
        if (OB_FAIL(generate_telemetry_container_scope_id(
            runtime_id, runtime_id_len, TELEMETRY_CONTAINER_RUNTIME_ID_SOURCE,
            scope_id, scope_id_size, scope_id_len))) {
        } else {
          has_scope_id = true;
        }
      }
    }
    if (OB_SUCC(ret) && container_detected && !has_scope_id) {
      char hostname[256] = {'\0'};
      if (0 != gethostname(hostname, sizeof(hostname) - 1)) {
        ret = OB_ERR_SYS;
        LOG_WARN("Failed to get container hostname for telemetry scope", K(ret), K(errno));
      } else {
        const int64_t hostname_len = static_cast<int64_t>(strlen(hostname));
        if (0 == hostname_len) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("Empty container hostname for telemetry scope", K(ret));
        } else if (!normalize_telemetry_default_container_hostname(hostname, hostname_len)) {
          ret = OB_NOT_SUPPORTED;
          LOG_WARN("Container runtime ID is unavailable; configure a telemetry instance ID",
                   K(ret), K(hostname_len));
        } else {
          if (OB_FAIL(generate_telemetry_container_scope_id(
              hostname, hostname_len, TELEMETRY_CONTAINER_HOSTNAME_SOURCE,
              scope_id, scope_id_size, scope_id_len))) {
          } else {
            has_scope_id = true;
          }
        }
      }
      MEMSET(hostname, 0, sizeof(hostname));
    }
    MEMSET(runtime_id, 0, sizeof(runtime_id));
#endif
  }
  return ret;
}

#ifdef _WIN32
static int get_telemetry_stable_machine_id(char *machine_id,
                                           const int64_t machine_id_len,
                                           int64_t &value_len)
{
  int ret = OB_SUCCESS;
  HKEY key = nullptr;
  char raw_id[128] = {'\0'};
  DWORD raw_id_len = sizeof(raw_id);
  DWORD value_type = 0;
  value_len = 0;
  LONG win_ret = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                               "SOFTWARE\\Microsoft\\Cryptography",
                               0, KEY_READ | KEY_WOW64_64KEY, &key);
  if (ERROR_SUCCESS != win_ret) {
    ret = OB_ERR_SYS;
  } else {
    win_ret = RegQueryValueExA(key, "MachineGuid", nullptr, &value_type,
                              reinterpret_cast<LPBYTE>(raw_id), &raw_id_len);
    if (ERROR_SUCCESS != win_ret || (REG_SZ != value_type && REG_EXPAND_SZ != value_type)) {
      ret = OB_ERR_SYS;
    } else {
      raw_id[sizeof(raw_id) - 1] = '\0';
      value_len = strlen(raw_id);
      unsigned char parsed_id[TELEMETRY_MACHINE_ID_BYTE_LENGTH] = {0};
      if (OB_FAIL(parse_telemetry_uuid_text(raw_id, value_len, parsed_id, sizeof(parsed_id)))) {
      } else if (machine_id_len <= value_len) {
        ret = OB_SIZE_OVERFLOW;
      } else {
        MEMCPY(machine_id, raw_id, value_len + 1);
      }
      MEMSET(parsed_id, 0, sizeof(parsed_id));
    }
    RegCloseKey(key);
  }
  MEMSET(raw_id, 0, sizeof(raw_id));
  return ret;
}
#elif defined(__APPLE__)
static int get_telemetry_stable_machine_id(char *machine_id,
                                           const int64_t machine_id_len,
                                           int64_t &value_len)
{
  int ret = OB_SUCCESS;
  uuid_t host_uuid = {0};
  struct timespec wait = {1, 0};
  value_len = 0;
  if (0 != gethostuuid(host_uuid, &wait)) {
    ret = OB_ERR_SYS;
  } else if (OB_FAIL(to_hex_cstr(host_uuid, sizeof(host_uuid), machine_id, machine_id_len))) {
    LOG_WARN("Failed to format macOS host UUID", K(ret));
  } else {
    value_len = strlen(machine_id);
  }
  return ret;
}
#elif defined(__linux__) || defined(__ANDROID__)
static int get_telemetry_stable_machine_id(char *machine_id,
                                           const int64_t machine_id_len,
                                           int64_t &value_len)
{
  int ret = OB_ENTRY_NOT_EXIST;
  value_len = 0;
  // machine-id is stable for one OS installation. The dbus path supports
  // older distributions that predate /etc/machine-id. VM/container images
  // must clear machine-id before cloning so that first boot provisions a new
  // identity for each clone.
  static const char *MACHINE_ID_FILES[] = {
    "/etc/machine-id",
    "/var/lib/dbus/machine-id"
  };
  for (int64_t i = 0; i < ARRAYSIZEOF(MACHINE_ID_FILES) && OB_SUCCESS != ret; ++i) {
    ret = read_telemetry_uuid_file(MACHINE_ID_FILES[i], machine_id, machine_id_len, value_len);
  }
  return ret;
}
#else
static int get_telemetry_stable_machine_id(char *machine_id,
                                           const int64_t machine_id_len,
                                           int64_t &value_len)
{
  UNUSED(machine_id);
  UNUSED(machine_id_len);
  value_len = 0;
  return OB_NOT_SUPPORTED;
}
#endif

static int get_telemetry_base_dir(char *base_dir,
                                  const int64_t base_dir_size,
                                  int64_t &base_dir_len)
{
  int ret = OB_SUCCESS;
  base_dir_len = 0;
  if (OB_ISNULL(base_dir) || base_dir_size <= 1) {
    ret = OB_INVALID_ARGUMENT;
  } else {
#ifdef _WIN32
    HANDLE dir_handle = CreateFileW(L".", 0,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (INVALID_HANDLE_VALUE == dir_handle) {
      ret = OB_ERR_SYS;
      const DWORD win_error = GetLastError();
      LOG_WARN("Failed to open telemetry base directory", K(ret), K(win_error));
    } else {
      wchar_t wide_path[common::OB_MAX_FILE_NAME_LENGTH] = {L'\0'};
      DWORD wide_path_len = GetFinalPathNameByHandleW(
          dir_handle, wide_path, ARRAYSIZEOF(wide_path),
          FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
      if (0 == wide_path_len) {
        // FILE_NAME_OPENED avoids per-component normalization failures on
        // network shares while retaining the same DOS path representation.
        wide_path_len = GetFinalPathNameByHandleW(
            dir_handle, wide_path, ARRAYSIZEOF(wide_path),
            FILE_NAME_OPENED | VOLUME_NAME_DOS);
      }
      if (0 == wide_path_len) {
        // A local volume without a DOS drive/mount name can still have a
        // stable volume GUID path.
        wide_path_len = GetFinalPathNameByHandleW(
            dir_handle, wide_path, ARRAYSIZEOF(wide_path),
            FILE_NAME_NORMALIZED | VOLUME_NAME_GUID);
      }
      if (0 == wide_path_len) {
        ret = OB_ERR_SYS;
        const DWORD win_error = GetLastError();
        LOG_WARN("Failed to canonicalize telemetry base directory", K(ret), K(win_error));
      } else if (wide_path_len >= ARRAYSIZEOF(wide_path)) {
        ret = OB_SIZE_OVERFLOW;
      } else {
        const int utf8_len = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, wide_path, static_cast<int>(wide_path_len),
            nullptr, 0, nullptr, nullptr);
        if (utf8_len <= 0) {
          ret = OB_ERR_SYS;
          const DWORD win_error = GetLastError();
          LOG_WARN("Failed to size telemetry base directory UTF-8 path", K(ret), K(win_error));
        } else if (base_dir_size <= utf8_len) {
          ret = OB_SIZE_OVERFLOW;
        } else if (utf8_len != WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, wide_path, static_cast<int>(wide_path_len),
            base_dir, static_cast<int>(base_dir_size - 1), nullptr, nullptr)) {
          ret = OB_ERR_SYS;
          const DWORD win_error = GetLastError();
          LOG_WARN("Failed to encode telemetry base directory as UTF-8", K(ret), K(win_error));
        } else {
          base_dir[utf8_len] = '\0';
          base_dir_len = utf8_len;
        }
      }
      CloseHandle(dir_handle);
    }
#else
    char *real_path = realpath(".", nullptr);
    if (OB_ISNULL(real_path)) {
      ret = OB_ERR_SYS;
    } else {
      const int64_t real_path_len = strlen(real_path);
      if (base_dir_size <= real_path_len) {
        ret = OB_SIZE_OVERFLOW;
      } else {
        MEMCPY(base_dir, real_path, real_path_len + 1);
        base_dir_len = real_path_len;
      }
      free(real_path);
    }
#endif
  }
  return ret;
}

int get_host_hash(char *buf, const int64_t buf_len)
{
  int ret = OB_SUCCESS;
  int64_t out_len = 0;
  char ip_buf[MAX_IP_ADDR_LENGTH] = {'\0'};
  char hash_buf[SHA256_DIGEST_LENGTH + 1] = {'\0'};
  ObAddr addr = GCONF.self_addr_;
  if (!addr.ip_to_string(ip_buf, sizeof(ip_buf))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObHashUtil::hash(OB_HASH_SH256, ip_buf, strlen(ip_buf), hash_buf, sizeof(hash_buf), out_len))) {
  } else if (OB_FAIL(to_hex_cstr(hash_buf, out_len, buf, buf_len))) {
  }
  return ret;
}

static int generate_id(char *id, const int64_t id_len)
{
  int ret = OB_SUCCESS;
  int64_t machine_id_len = 0;
  int64_t base_dir_len = 0;
  int64_t scope_id_len = 0;
  bool has_scope_id = false;
  char machine_id[128] = {'\0'};
  char base_dir[common::OB_MAX_FILE_NAME_LENGTH] = {'\0'};
  char scope_id[TELEMETRY_UUID_STRING_LENGTH + 1] = {'\0'};
  if (OB_FAIL(get_telemetry_container_scope_id(
      scope_id, sizeof(scope_id), scope_id_len, has_scope_id))) {
  } else {
    const int machine_id_ret = get_telemetry_stable_machine_id(
        machine_id, sizeof(machine_id), machine_id_len);
    if (OB_SUCCESS != machine_id_ret) {
      if (has_scope_id) {
        // Minimal container images may not carry an OS machine-id. The scope ID
        // is already a stable UUID for this container and can safely key the
        // outer derivation without introducing base-directory state.
        MEMCPY(machine_id, scope_id, scope_id_len + 1);
        machine_id_len = scope_id_len;
      } else {
        ret = machine_id_ret;
        LOG_WARN("Failed to get a stable machine ID for telemetry", K(ret));
      }
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(get_telemetry_base_dir(
      base_dir, sizeof(base_dir), base_dir_len))) {
    LOG_WARN("Failed to get the canonical base directory for telemetry", K(ret));
  } else if (OB_SUCC(ret) && OB_FAIL(generate_telemetry_uuid(
      machine_id, machine_id_len, base_dir, base_dir_len,
      has_scope_id ? scope_id : nullptr, has_scope_id ? scope_id_len : 0,
      id, id_len))) {
    LOG_WARN("Failed to generate stable telemetry UUID", K(ret));
  }
  MEMSET(machine_id, 0, sizeof(machine_id));
  MEMSET(base_dir, 0, sizeof(base_dir));
  MEMSET(scope_id, 0, sizeof(scope_id));
  return ret;
}

int generate_telemetry_json(const char* reporter, const char* event_name, ObIAllocator *allocator, ObString &json_str)
{
  int ret = OB_SUCCESS;
  const int64_t SHA256_DIGEST_HEX_LEN = 2 * SHA256_DIGEST_LENGTH + 1;
  const int64_t OS_INFO_LEN = 32;
  const int64_t CPU_MODEL_LEN = 64;
  const int64_t SIZE_STR_LEN = 16;
  ObJsonObject root(allocator);
  ObJsonObject host(allocator);
  ObJsonObject instance(allocator);
  ObJsonObject resource(allocator);
  ObJsonObject content(allocator);
  char os_name[OS_INFO_LEN] = {'\0'};
  char os_version[OS_INFO_LEN] = {'\0'};
  char cpu_model[CPU_MODEL_LEN] = {'\0'};
  char host_hash[SHA256_DIGEST_HEX_LEN + 1] = {'\0'};
  char id[TELEMETRY_UUID_STRING_LENGTH + 1] = {'\0'};
  int64_t ts = ObTimeUtility::fast_current_time();
  int64_t cpu_count = common::get_cpu_count();
  int64_t host_cpu_count = common::get_cpu_num();
  int64_t port = GCONF.mysql_port;
  char version[OB_SERVER_VERSION_LENGTH] = {'\0'};
  char memory_budget[SIZE_STR_LEN] = {'\0'};
  char host_memory_size[SIZE_STR_LEN] = {'\0'};
  char log_disk_size[SIZE_STR_LEN] = {'\0'};
  char datafile_size[SIZE_STR_LEN] = {'\0'};

  // construct report content
  double memory_budget_gb = static_cast<double>(lib::get_memory_budget()) / 1024 / 1024 / 1024;
  double host_memory_size_gb = static_cast<double>(common::get_phy_mem_size()) / 1024 / 1024 / 1024;
  double log_disk_size_gb = static_cast<double>(GCONF.log_disk_size) / 1024 / 1024 / 1024;
  double datafile_size_gb = static_cast<double>(GCONF.datafile_size) / 1024 / 1024 / 1024;
  snprintf(memory_budget, sizeof(memory_budget), "%.9gG", memory_budget_gb);
  snprintf(host_memory_size, sizeof(host_memory_size), "%.9gG", host_memory_size_gb);
  snprintf(log_disk_size, sizeof(log_disk_size), "%.9gG", log_disk_size_gb);
  snprintf(datafile_size, sizeof(datafile_size), "%.9gG", datafile_size_gb);
  VersionUtil::print_version_str(version, sizeof(version), SERVER_CURRENT_VERSION);
  get_host_hash(host_hash, sizeof(host_hash));
  get_os_info(os_name, sizeof(os_name), os_version, sizeof(os_version));
  get_cpu_model(cpu_model, sizeof(cpu_model));
  if (OB_FAIL(generate_id(id, sizeof(id)))) {
  }

  // construct host
  ObJsonString os_json(os_name);
  ObJsonString os_version_json(os_version);
  ObJsonString cpu_json(cpu_model);
  ObJsonInt host_cpu_count_json(host_cpu_count);
  ObJsonString host_memory_size_json(host_memory_size);
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(host.add("os", &os_json))) {
  } else if (OB_FAIL(host.add("osVersion", &os_version_json))) {
  } else if (OB_FAIL(host.add("cpu", &cpu_json))) {
  } else if (OB_FAIL(host.add("cpuCount", &host_cpu_count_json))) {
  } else if (OB_FAIL(host.add("memorySize", &host_memory_size_json))) {
  }

  // construct instance
  ObJsonString host_hash_json(host_hash);
  ObJsonInt port_json(port);
  ObJsonInt timestamp_json(ts);
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(instance.add("hostHash", &host_hash_json))) {
  } else if (OB_FAIL(instance.add("port", &port_json))) {
  } else if (OB_FAIL(instance.add("createTimestamp", &timestamp_json))) {
  }

  // construct resource
  ObJsonInt cpu_count_json(cpu_count);
  ObJsonString memory_budget_json(memory_budget);
  ObJsonString log_disk_size_json(log_disk_size);
  ObJsonString datafile_size_json(datafile_size);
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(resource.add("cpuCount", &cpu_count_json))) {
  // Keep the legacy JSON key for telemetry schema compatibility.
  } else if (OB_FAIL(resource.add("memoryLimit", &memory_budget_json))) {
  } else if (OB_FAIL(resource.add("logDiskSize", &log_disk_size_json))) {
  } else if (OB_FAIL(resource.add("dataFileSize", &datafile_size_json))) {
  }

  // construct content
  ObJsonString id_json(id);
  ObJsonString version_json(version);
  ObJsonString reporter_json(reporter);
  ObJsonString event_json(event_name);
  ObJsonInt telemetry_version_json(TELEMETRY_VERSION);
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(content.add("id", &id_json))) {
  } else if (OB_FAIL(content.add("version", &version_json))) {
  } else if (OB_FAIL(content.add("reporter", &reporter_json))) {
  } else if (OB_FAIL(content.add("host", &host))) {
  } else if (OB_FAIL(content.add("instance", &instance))) {
  } else if (OB_FAIL(content.add("resource", &resource))) {
  } else if (OB_FAIL(content.add("event", &event_json))) {
  } else if (OB_FAIL(content.add("telemetryVersion", &telemetry_version_json))) {
  }

  // construct root
  ObJsonString component_json(OB_SEEKDB_NAME);
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(root.add("content", &content))) {
  } else if (OB_FAIL(root.add("component", &component_json))) {
  }

  ObJsonBuffer j_buf(allocator);
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(root.print(j_buf, false))) {
  } else {
    json_str.assign_ptr(j_buf.ptr(), j_buf.length());
  }

  if (OB_SUCC(ret)) {
    FILE *fp = fopen(TELEMETRY_FILE_NAME, "w");
    if (OB_NOT_NULL(fp)) {
      if (json_str.length() != fwrite(json_str.ptr(), 1, json_str.length(), fp)) {
        ret = OB_IO_ERROR;
        LOG_WARN("Failed to write telemetry to file", K(ret));
      }
      fclose(fp);
    }
  }

  return ret;
}

static size_t discard(void* ptr, size_t size, size_t nmemb, void* userdata) {
  return size * nmemb;
}

int send_telemetry_by_libcurl(const char *url, const ObString &json_str)
{
  int ret = OB_SUCCESS;
  CURL *curl = curl_easy_init();
  if (curl == nullptr) {
    LOG_WARN("Failed to init curl");
    ret = OB_CURL_ERROR;
  } else {
    CURLcode cc = CURLE_OK;
    struct curl_slist *list = NULL;
    // set post options
    if (NULL == (list = curl_slist_append(list, "Content-Type: application/json"))) {
      ret = OB_CURL_ERROR;
      LOG_WARN("append list failed", K(ret));
    } else {
      curl_easy_setopt(curl, CURLOPT_URL, url);
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
      curl_easy_setopt(curl, CURLOPT_POST, 1L);
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, json_str.length());
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str.ptr());
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);

      // set other options
      const int64_t no_signal = 1;
      const int64_t timeout_ms = 1000; // 1s
      const int64_t no_delay = 1;
      const int64_t max_redirect = 3; // set max redirect
      const int64_t follow_location = 1; // for http redirect 301 302
      curl_easy_setopt(curl, CURLOPT_NOSIGNAL, no_signal);
      curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
      curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, timeout_ms);
      curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, no_delay);
      curl_easy_setopt(curl, CURLOPT_MAXREDIRS, max_redirect);
      curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, follow_location);

      // send request and do not care about the http code
      if (CURLE_OK != (cc = curl_easy_perform(curl))) {
        LOG_WARN("Failed to perform curl", K(cc));
        ret = OB_CURL_ERROR;
      }
      curl_slist_free_all(list);
    }
    curl_easy_cleanup(curl);
  }
  return ret;
}

int send_telemetry(const char *url, const ObString &json_str)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(url) || json_str.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument", K(ret), KP(url), K(json_str));
  } else {
    ret = send_telemetry_by_libcurl(url, json_str);
  }
  return ret;
}

bool is_telemetry_enabled()
{
  bool bret = true;
  const char* telemetry_enabled = getenv("TELEMETRY_ENABLED");
  if (NULL != telemetry_enabled && 0 == STRCMP(telemetry_enabled, "false")) {
    bret = false;
  }
  return bret;
}

int report_telemetry(const char *reporter, const char *event_name)
{
  int ret = OB_SUCCESS;
  common::ObArenaAllocator allocator;
  ObString json_str;
  if (OB_FAIL(generate_telemetry_json(reporter, event_name, &allocator, json_str))) {
  } else if (is_telemetry_enabled()
             && OB_FAIL(send_telemetry(TELEMETRY_URL, json_str))) {
    LOG_WARN("Failed to send telemetry", K(ret));
  }
  return ret;
}

}
}
