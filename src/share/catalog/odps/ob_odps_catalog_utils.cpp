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

#define USING_LOG_PREFIX SHARE
#include "share/catalog/odps/ob_odps_catalog_utils.h"

#include "share/catalog/ob_catalog_properties.h"

namespace oceanbase
{
namespace share
{

#ifdef OB_BUILD_CPP_ODPS
int ObODPSCatalogUtils::create_odps_conf(const ObODPSCatalogProperties &odps_format, apsara::odps::sdk::Configuration &conf)
{
  int ret = OB_SUCCESS;
  apsara::odps::sdk::Account account;

  if (0 == odps_format.access_type_.case_compare("aliyun") || odps_format.access_type_.empty()) {
    account = apsara::odps::sdk::Account(std::string(apsara::odps::sdk::ACCOUNT_ALIYUN),
                                         std::string(odps_format.access_id_.ptr(), odps_format.access_id_.length()),
                                         std::string(odps_format.access_key_.ptr(), odps_format.access_key_.length()));
  } else if (0 == odps_format.access_type_.case_compare("sts")) {
    account = apsara::odps::sdk::Account(std::string(apsara::odps::sdk::ACCOUNT_STS),
                                         std::string(odps_format.sts_token_.ptr(), odps_format.sts_token_.length()));
  } else if (0 == odps_format.access_type_.case_compare("token")) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("unsupported access type", K(ret), K(odps_format.access_type_));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "ODPS access type: token");
  } else if (0 == odps_format.access_type_.case_compare("domain")) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("unsupported access type", K(ret), K(odps_format.access_type_));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "ODPS access type: domain");
  } else if (0 == odps_format.access_type_.case_compare("taobao")) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("unsupported access type", K(ret), K(odps_format.access_type_));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "ODPS access type: taobao");
  } else if (0 == odps_format.access_type_.case_compare("app")) {
    account = apsara::odps::sdk::Account(std::string(apsara::odps::sdk::ACCOUNT_APPLICATION),
                                         std::string(odps_format.access_id_.ptr(), odps_format.access_id_.length()),
                                         std::string(odps_format.access_key_.ptr(), odps_format.access_key_.length()));
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "ODPS access type");
  }
  conf.SetAccount(account);
  conf.SetEndpoint(std::string(odps_format.endpoint_.ptr(), odps_format.endpoint_.length()));
  if (!odps_format.tunnel_endpoint_.empty()) {
    LOG_TRACE("set tunnel endpoint", K(ret), K(odps_format.tunnel_endpoint_));
    conf.SetTunnelEndpoint(std::string(odps_format.tunnel_endpoint_.ptr(), odps_format.tunnel_endpoint_.length()));
  }
  conf.SetUserAgent("OB_ACCESS_ODPS");
  conf.SetTunnelQuotaName(std::string(odps_format.quota_.ptr(), odps_format.quota_.length()));
  if (0 == odps_format.compression_code_.case_compare("zlib")) {
    conf.SetCompressOption(apsara::odps::sdk::CompressOption::ZLIB_COMPRESS);
  } else if (0 == odps_format.compression_code_.case_compare("zstd")) {
    conf.SetCompressOption(apsara::odps::sdk::CompressOption::ZSTD_COMPRESS);
  } else if (0 == odps_format.compression_code_.case_compare("lz4")) {
    conf.SetCompressOption(apsara::odps::sdk::CompressOption::LZ4_COMPRESS);
  } else if (0 == odps_format.compression_code_.case_compare("odps_lz4")) {
    conf.SetCompressOption(apsara::odps::sdk::CompressOption::ODPS_LZ4_COMPRESS);
  } else {
    conf.SetCompressOption(apsara::odps::sdk::CompressOption::NO_COMPRESS);
  }

  return ret;
}
#endif

} // namespace share
} // namespace oceanbase
