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

#define USING_LOG_PREFIX SQL

#include "ob_catalog_properties.h"

#include "lib/list/ob_dlist.h"
#include "lib/oblog/ob_log_module.h"
#include "common/ob_hex_utils_base.h"
#include "lib/utility/ob_print_utils.h"
#include "share/ob_encryption_util.h"
#include "share/rc/ob_tenant_base.h"
#include "share/ob_define.h"

namespace oceanbase
{
using namespace common;
using namespace sql;
namespace share
{
const char *ObCatalogProperties::CATALOG_TYPE_STR[] = {"ODPS"};
static_assert(array_elements(ObCatalogProperties::CATALOG_TYPE_STR) == static_cast<size_t>(ObCatalogProperties::CatalogType::MAX_TYPE), "Not enough initializer for ObCatalogProperties");
static_assert(array_elements(ObODPSCatalogProperties::OPTION_NAMES) == static_cast<size_t>(ObODPSCatalogProperties::ObOdpsCatalogOptions::MAX_OPTIONS), "Not enough initializer for ObODPSCatalogProperties");

int ObCatalogProperties::to_string_with_alloc(ObString &str, ObIAllocator &allocator) const
{
  int ret = OB_SUCCESS;
  char *buf = NULL;
  int64_t buf_len = DEFAULT_BUF_LENGTH / 2;
  int64_t pos = 0;
  do {
    buf_len *= 2;
    ret = OB_SUCCESS;
    if (OB_ISNULL(buf = static_cast<char *>(allocator.alloc(buf_len)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc buf", K(ret), K(buf_len));
    } else if (OB_FAIL(to_string(buf, buf_len, pos))) {
      LOG_WARN("failed to write string", K(ret));
    }
  } while (OB_SIZE_OVERFLOW == ret);
  OX(str.assign_ptr(buf, pos));
  return ret;
}


int ObCatalogProperties::to_string(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  bool is_valid_format = type_ > CatalogType::INVALID_TYPE && type_ < CatalogType::MAX_TYPE;
  OZ(J_OBJ_START());
  OZ(databuff_print_kv(buf,
                       buf_len,
                       pos,
                       R"("TYPE")",
                       is_valid_format
                           ? ObCatalogProperties::CATALOG_TYPE_STR[static_cast<size_t>(type_)]
                           : "INVALID"));
  if (is_valid_format) {
    OZ(to_json_kv_string(buf, buf_len, pos));
  }
  OZ(J_OBJ_END());
  return ret;
}

int ObCatalogProperties::parse_catalog_type(const ObString &str, CatalogType &type)
{
  int ret = OB_SUCCESS;
  json::Value *data = NULL;
  json::Parser parser;
  ObArenaAllocator temp_allocator;
  if (OB_UNLIKELY(str.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("format string is empty", K(ret), K(str));
  } else if (OB_FAIL(parser.init(&temp_allocator))) {
    LOG_WARN("parser init failed", K(ret));
  } else if (OB_FAIL(parser.parse(str.ptr(), str.length(), data))) {
    LOG_WARN("parse json failed", K(ret), K(str));
  } else if (NULL == data || json::JT_OBJECT != data->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error json value", K(ret), KPC(data));
  } else {
    json::Pair *type_node = data->get_object().get_first();
    if (type_node->value_->get_type() != json::JT_STRING) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected json format", K(ret), K(str));
    } else {
      ObString type_str = type_node->value_->get_string();
      for (int i = 0; i < array_elements(ObCatalogProperties::CATALOG_TYPE_STR); ++i) {
        if (type_str.case_compare(ObCatalogProperties::CATALOG_TYPE_STR[i]) == 0) {
          type = static_cast<CatalogType>(i);
          break;
        }
      }
    }
  }
  return ret;
}

// moved definition to sql/resolver/ddl/ob_ddl_resolver.cpp(parser vocabulary)

int ObCatalogProperties::encrypt_str(ObString &src, ObString &dst, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  dst = src;
  return ret;
}

int ObCatalogProperties::decrypt_str(ObString &src, ObString &dst, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  dst = src;
  return ret;
}

int ObODPSCatalogProperties::encrypt(ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  ObString encrypted_access_id;
  ObString encrypted_access_key;
  ObString encrypted_sts_token;
  if (OB_FAIL(encrypt_str(access_id_, encrypted_access_id, allocator))) {
    LOG_WARN("failed to encrypt", K(ret));
  } else if (OB_FAIL(encrypt_str(access_key_, encrypted_access_key, allocator))) {
    LOG_WARN("failed to encrypt", K(ret));
  } else if (OB_FAIL(encrypt_str(sts_token_, encrypted_sts_token, allocator))) {
    LOG_WARN("failed to encrypt", K(ret));
  } else {
    access_id_ = encrypted_access_id;
    access_key_ = encrypted_access_key;
    sts_token_ = encrypted_sts_token;
  }
  return ret;
}

int ObODPSCatalogProperties::decrypt(ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  ObString decrypted_access_id;
  ObString decrypted_access_key;
  ObString decrypted_sts_token;
  if (OB_FAIL(decrypt_str(access_id_, decrypted_access_id, allocator))) {
    LOG_WARN("failed to encrypt", K(ret));
  } else if (OB_FAIL(decrypt_str(access_key_, decrypted_access_key, allocator))) {
    LOG_WARN("failed to encrypt", K(ret));
  } else if (OB_FAIL(decrypt_str(sts_token_, decrypted_sts_token, allocator))) {
    LOG_WARN("failed to encrypt", K(ret));
  } else {
    access_id_ = decrypted_access_id;
    access_key_ = decrypted_access_key;
    sts_token_ = decrypted_sts_token;
  }
  return ret;
}

int ObODPSCatalogProperties::to_json_kv_string(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;

  OZ(J_COMMA());
  {
    ObCStringHelper helper;
    OZ(databuff_printf(buf,
                       buf_len,
                       pos,
                       R"("%s":"%s")",
                       OPTION_NAMES[static_cast<size_t>(ObOdpsCatalogOptions::ACCESSTYPE)],
                       helper.convert(ObHexStringWrap(access_type_))));
  }
  OZ(J_COMMA());
  {
    ObCStringHelper helper;
    OZ(databuff_printf(buf,
                       buf_len,
                       pos,
                       R"("%s":"%s")",
                       OPTION_NAMES[static_cast<size_t>(ObOdpsCatalogOptions::ACCESSID)],
                       helper.convert(ObHexStringWrap(access_id_))));
  }
  OZ(J_COMMA());
  {
    ObCStringHelper helper;
    OZ(databuff_printf(buf,
                       buf_len,
                       pos,
                       R"("%s":"%s")",
                       OPTION_NAMES[static_cast<size_t>(ObOdpsCatalogOptions::ACCESSKEY)],
                       helper.convert(ObHexStringWrap(access_key_))));
  }
  OZ(J_COMMA());
  {
    ObCStringHelper helper;
    OZ(databuff_printf(buf,
                       buf_len,
                       pos,
                       R"("%s":"%s")",
                       OPTION_NAMES[static_cast<size_t>(ObOdpsCatalogOptions::STSTOKEN)],
                       helper.convert(ObHexStringWrap(sts_token_))));
  }
  OZ(J_COMMA());
  {
    ObCStringHelper helper;
    OZ(databuff_printf(buf,
                       buf_len,
                       pos,
                       R"("%s":"%s")",
                       OPTION_NAMES[static_cast<size_t>(ObOdpsCatalogOptions::ENDPOINT)],
                       helper.convert(ObHexStringWrap(endpoint_))));
  }
  OZ(J_COMMA());
  {
    ObCStringHelper helper;
    OZ(databuff_printf(buf,
                       buf_len,
                       pos,
                       R"("%s":"%s")",
                       OPTION_NAMES[static_cast<size_t>(ObOdpsCatalogOptions::TUNNEL_ENDPOINT)],
                       helper.convert(ObHexStringWrap(tunnel_endpoint_))));
  }
  OZ(J_COMMA());
  {
    ObCStringHelper helper;
    OZ(databuff_printf(buf,
                       buf_len,
                       pos,
                       R"("%s":"%s")",
                       OPTION_NAMES[static_cast<size_t>(ObOdpsCatalogOptions::PROJECT_NAME)],
                       helper.convert(ObHexStringWrap(project_))));
  }
  OZ(J_COMMA());
  {
    ObCStringHelper helper;
    OZ(databuff_printf(buf,
                       buf_len,
                       pos,
                       R"("%s":"%s")",
                       OPTION_NAMES[static_cast<size_t>(ObOdpsCatalogOptions::QUOTA_NAME)],
                       helper.convert(ObHexStringWrap(quota_))));
  }
  OZ(J_COMMA());
  {
    ObCStringHelper helper;
    OZ(databuff_printf(buf,
                       buf_len,
                       pos,
                       R"("%s":"%s")",
                       OPTION_NAMES[static_cast<size_t>(ObOdpsCatalogOptions::COMPRESSION_CODE)],
                       helper.convert(ObHexStringWrap(compression_code_))));
  }
  OZ(J_COMMA());
  {
    ObCStringHelper helper;
    OZ(databuff_printf(buf,
                       buf_len,
                       pos,
                       R"("%s":"%s")",
                       OPTION_NAMES[static_cast<size_t>(ObOdpsCatalogOptions::REGION)],
                       helper.convert(ObHexStringWrap(region_))));
  }

  return ret;
}

int ObODPSCatalogProperties::load_from_string(const ObString &str, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  json::Value *data = NULL;
  json::Parser parser;
  ObArenaAllocator temp_allocator;
  json::Pair *node = NULL;
  if (OB_UNLIKELY(str.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("format string is empty", K(ret), K(str));
  } else if (OB_FAIL(parser.init(&temp_allocator))) {
    LOG_WARN("parser init failed", K(ret));
  } else if (OB_FAIL(parser.parse(str.ptr(), str.length(), data))) {
    LOG_WARN("parse json failed", K(ret), K(str));
  } else if (NULL == data || json::JT_OBJECT != data->get_type()
             || OB_ISNULL(node = data->get_object().get_first())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error json value", K(ret), KPC(data));
  } else {
    node = node->get_next();
    if (OB_NOT_NULL(node) && 0 == node->name_.case_compare(OPTION_NAMES[static_cast<size_t>(ObOdpsCatalogOptions::ACCESSTYPE)])
        && json::JT_STRING == node->value_->get_type()) {
      ObObj obj;
      OZ(ObHexUtilsBase::unhex(node->value_->get_string(), allocator, obj));
      if (OB_SUCC(ret) && !obj.is_null()) {
        access_type_ = obj.get_string();
      }
      node = node->get_next();
    }
    if (OB_SUCC(ret) && OB_NOT_NULL(node) && 0 == node->name_.case_compare(OPTION_NAMES[static_cast<size_t>(ObOdpsCatalogOptions::ACCESSID)])
        && json::JT_STRING == node->value_->get_type()) {
      ObObj obj;
      OZ(ObHexUtilsBase::unhex(node->value_->get_string(), allocator, obj));
      if (OB_SUCC(ret) && !obj.is_null()) {
        access_id_ = obj.get_string();
      }
      node = node->get_next();
    }
    if (OB_NOT_NULL(node) && 0 == node->name_.case_compare(OPTION_NAMES[static_cast<size_t>(ObOdpsCatalogOptions::ACCESSKEY)])
        && json::JT_STRING == node->value_->get_type()) {
      ObObj obj;
      OZ(ObHexUtilsBase::unhex(node->value_->get_string(), allocator, obj));
      if (OB_SUCC(ret) && !obj.is_null()) {
        access_key_ = obj.get_string();
      }
      node = node->get_next();
    }
    if (OB_NOT_NULL(node) && 0 == node->name_.case_compare(OPTION_NAMES[static_cast<size_t>(ObOdpsCatalogOptions::STSTOKEN)])
        && json::JT_STRING == node->value_->get_type()) {
      ObObj obj;
      OZ(ObHexUtilsBase::unhex(node->value_->get_string(), allocator, obj));
      if (OB_SUCC(ret) && !obj.is_null()) {
        sts_token_ = obj.get_string();
      }
      node = node->get_next();
    }
    if (OB_NOT_NULL(node) && 0 == node->name_.case_compare(OPTION_NAMES[static_cast<size_t>(ObOdpsCatalogOptions::ENDPOINT)])
        && json::JT_STRING == node->value_->get_type()) {
      ObObj obj;
      OZ(ObHexUtilsBase::unhex(node->value_->get_string(), allocator, obj));
      if (OB_SUCC(ret) && !obj.is_null()) {
        endpoint_ = obj.get_string();
      }
      node = node->get_next();
    }
    if (OB_NOT_NULL(node) && 0 == node->name_.case_compare(OPTION_NAMES[static_cast<size_t>(ObOdpsCatalogOptions::TUNNEL_ENDPOINT)])
        && json::JT_STRING == node->value_->get_type()) {
      ObObj obj;
      OZ(ObHexUtilsBase::unhex(node->value_->get_string(), allocator, obj));
      if (OB_SUCC(ret) && !obj.is_null()) {
        tunnel_endpoint_ = obj.get_string();
      }
      node = node->get_next();
    }
    if (OB_NOT_NULL(node) && 0 == node->name_.case_compare(OPTION_NAMES[static_cast<size_t>(ObOdpsCatalogOptions::PROJECT_NAME)])
        && json::JT_STRING == node->value_->get_type()) {
      ObObj obj;
      OZ(ObHexUtilsBase::unhex(node->value_->get_string(), allocator, obj));
      if (OB_SUCC(ret) && !obj.is_null()) {
        project_ = obj.get_string();
      }
      node = node->get_next();
    }
    if (OB_NOT_NULL(node) && 0 == node->name_.case_compare(OPTION_NAMES[static_cast<size_t>(ObOdpsCatalogOptions::QUOTA_NAME)])
        && json::JT_STRING == node->value_->get_type()) {
      ObObj obj;
      OZ(ObHexUtilsBase::unhex(node->value_->get_string(), allocator, obj));
      if (OB_SUCC(ret) && !obj.is_null()) {
        quota_ = obj.get_string();
      }
      node = node->get_next();
    }
    if (OB_NOT_NULL(node) && 0 == node->name_.case_compare(OPTION_NAMES[static_cast<size_t>(ObOdpsCatalogOptions::COMPRESSION_CODE)])
        && json::JT_STRING == node->value_->get_type()) {
      ObObj obj;
      OZ(ObHexUtilsBase::unhex(node->value_->get_string(), allocator, obj));
      if (OB_SUCC(ret) && !obj.is_null()) {
        compression_code_ = obj.get_string();
      }
      node = node->get_next();
    }
    if (OB_NOT_NULL(node) && 0 == node->name_.case_compare(OPTION_NAMES[static_cast<size_t>(ObOdpsCatalogOptions::REGION)])
        && json::JT_STRING == node->value_->get_type()) {
      ObObj obj;
      OZ(ObHexUtilsBase::unhex(node->value_->get_string(), allocator, obj));
      if (OB_SUCC(ret) && !obj.is_null()) {
        region_ = obj.get_string();
      }
      node = node->get_next();
    }
  }
  return ret;
}

// moved definition to sql/resolver/ddl/ob_ddl_resolver.cpp(parser vocabulary)

} // namespace share
} // namespace oceanbase
