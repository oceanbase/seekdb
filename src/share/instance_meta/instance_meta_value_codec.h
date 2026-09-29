/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef OCEANBASE_SHARE_INSTANCE_META_VALUE_CODEC_H_
#define OCEANBASE_SHARE_INSTANCE_META_VALUE_CODEC_H_

#include <cstdint>
#include <string>
#include "lib/ob_errno.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace share
{
namespace instance_meta
{

enum class ValueFormat : char { JSON = 'J', BYTES = 'B' };

// The marker describes the payload, independently of its collection. A
// diagnostic reader can therefore render every row without collection cases.
class InstanceMetaValueCodec final
{
public:
  static int encode(ValueFormat format, const std::string &payload, std::string &value)
  {
    if (format != ValueFormat::JSON && format != ValueFormat::BYTES) {
      return common::OB_INVALID_ARGUMENT;
    }
    value.clear();
    value.reserve(payload.size() + 1);
    value.push_back(static_cast<char>(format));
    value.append(payload);
    return common::OB_SUCCESS;
  }

  static int decode(const common::ObString &value, ValueFormat &format,
                    common::ObString &payload)
  {
    if (value.length() < 1 || value.ptr() == nullptr) {
      return common::OB_CHECKSUM_ERROR;
    }
    format = static_cast<ValueFormat>(value.ptr()[0]);
    if (format != ValueFormat::JSON && format != ValueFormat::BYTES) {
      return common::OB_CHECKSUM_ERROR;
    }
    payload.assign_ptr(value.ptr() + 1, value.length() - 1);
    return common::OB_SUCCESS;
  }

  static void base64(const common::ObString &bytes, std::string &encoded)
  {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    encoded.clear();
    encoded.reserve(((bytes.length() + 2) / 3) * 4);
    const unsigned char *data = reinterpret_cast<const unsigned char *>(bytes.ptr());
    for (int64_t i = 0; i < bytes.length(); i += 3) {
      const uint32_t first = data[i];
      const uint32_t second = i + 1 < bytes.length() ? data[i + 1] : 0;
      const uint32_t third = i + 2 < bytes.length() ? data[i + 2] : 0;
      encoded.push_back(alphabet[first >> 2]);
      encoded.push_back(alphabet[((first & 3) << 4) | (second >> 4)]);
      encoded.push_back(i + 1 < bytes.length()
          ? alphabet[((second & 15) << 2) | (third >> 6)] : '=');
      encoded.push_back(i + 2 < bytes.length() ? alphabet[third & 63] : '=');
    }
  }
};

} // namespace instance_meta
} // namespace share
} // namespace oceanbase
#endif
