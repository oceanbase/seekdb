// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Frozen function bodies from a48096008ca8a5869afc0a290c07ba6ebf3ac4b9.
// Only function names changed; helpers are unchanged production helpers.
int baseline_generate_telemetry_uuid(const char *machine_id,
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

static int baseline_get_telemetry_base_dir(char *base_dir,
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
