/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#define USING_LOG_PREFIX SHARE_CONFIG

#include "ob_server_config.h"
#include "observer/ob_server.h"
namespace oceanbase
{
namespace common
{

int64_t get_cpu_count()
{
  int64_t cpu_cnt = GCONF.cpu_count;
  return cpu_cnt > 0 ? cpu_cnt : get_cpu_num();
}

using namespace share;

ObServerConfig::ObServerConfig()
    : disk_actual_space_(0), self_addr_(), system_config_(NULL)
{
}

ObServerConfig::~ObServerConfig()
{
}

ObServerConfig &ObServerConfig::get_instance()
{
  static ObServerConfig config;
  return config;
}

int ObServerConfig::init(const ObSystemConfig &config)
{
  int ret = OB_SUCCESS;
  system_config_ = &config;
  if (OB_ISNULL(system_config_)) {
    ret = OB_INIT_FAIL;
  }
  return ret;
}

bool ObServerConfig::in_upgrade_mode() const
{
  bool bret = false;
  if (enable_upgrade_mode) {
    bret = true;
  } else {
    obrpc::ObUpgradeStage stage = GCTX.get_upgrade_stage();
    bret = (stage >= obrpc::OB_UPGRADE_STAGE_PREUPGRADE
            && stage <= obrpc::OB_UPGRADE_STAGE_POSTUPGRADE);
  }
  return bret;
}


int ObServerConfig::read_config()
{
  int ret = OB_SUCCESS;
  int temp_ret = OB_SUCCESS;
  ObSystemConfigKey key;
  char local_ip[OB_MAX_SERVER_ADDR_SIZE] = "";
  if (OB_UNLIKELY(true != self_addr_.ip_to_string(local_ip, sizeof(local_ip)))) {
    ret = OB_CONVERT_ERROR;
  } else {
    key.set_varchar(ObString::make_string("svr_type"), print_server_role(get_server_type()));
    key.set_int(ObString::make_string("svr_port"), rpc_port);
    key.set_varchar(ObString::make_string("svr_ip"), local_ip);
    key.set_varchar(ObString::make_string("zone"), zone);
    ObConfigContainer::const_iterator it = container_.begin();
    for (; OB_SUCC(ret) && it != container_.end(); ++it) {
      key.set_name(it->first.str());
      if (OB_ISNULL(it->second)) {
        ret = OB_ERR_UNEXPECTED;
        OB_LOG(ERROR, "config item is null", "name", it->first.str(), K(ret));
      } else {
        key.set_version(it->second->version());
        temp_ret = system_config_->read_config(get_tenant_id(), key, *(it->second));
        if (OB_SUCCESS != temp_ret) {
          OB_LOG(DEBUG, "Read config error", "name", it->first.str(), K(temp_ret));
        }
      }
    }
  }
  return ret;
}

int ObServerConfig::check_all() const
{
  int ret = OB_SUCCESS;
  ObConfigContainer::const_iterator it = container_.begin();
  for (; OB_SUCC(ret) && it != container_.end(); ++it) {
    if (OB_ISNULL(it->second)) {
      ret = OB_ERR_UNEXPECTED;
      OB_LOG(ERROR, "config item is null", "name", it->first.str(), K(ret));
    } else if (!it->second->check()) {
      int temp_ret = OB_INVALID_CONFIG;
      OB_LOG_RET(WARN, temp_ret, "Configure setting invalid",
             "name", it->first.str(), "value", it->second->str(), K(temp_ret));
    } else {
      // do nothing
    }
  }
  return ret;
}

int ObServerConfig::strict_check_special() const
{
  int ret = OB_SUCCESS;
  if (OB_SUCC(ret)) {
    if (!cluster_id.check()) {
      ret = OB_INVALID_CONFIG;
      SHARE_LOG(WARN, "invalid cluster id", K(ret), K(cluster_id.str()));
    } else if (strlen(zone.str()) <= 0) {
      ret = OB_INVALID_CONFIG;
      SHARE_LOG(WARN, "config zone cannot be empty", KR(ret), K(zone.str()));
    }
  }
  return ret;
}

void ObServerConfig::print() const
{
  OB_LOG(INFO, "===================== *begin server config report * =====================");
  ObConfigContainer::const_iterator it = container_.begin();
  for (; it != container_.end(); ++it) {
    if (OB_ISNULL(it->second)) {
      OB_LOG_RET(WARN, OB_ERROR, "config item is null", "name", it->first.str());
    } else {
      _OB_LOG(INFO, "| %-36s = %s", it->first.str(), it->second->str());
    }
  }
  OB_LOG(INFO, "===================== *stop server config report* =======================");
}

int ObServerConfig::add_extra_config(const char *config_str,
                                     const int64_t version /* = 0 */,
                                     const bool check_config /* = true */)
{
  DRWLock::WRLockGuard guard(OTC_MGR.rwlock_);
  return add_extra_config_unsafe(config_str, version, check_config);
}

double ObServerConfig::get_sys_tenant_default_min_cpu()
{
  double min_cpu = server_cpu_quota_min;
  if (0 == min_cpu) {
    int64_t cpu_count = get_cpu_count();
    if (cpu_count < 8) {
      min_cpu = 1;
    } else if (cpu_count < 16) {
      min_cpu = 2;
    } else if (cpu_count < 32) {
      min_cpu = 3;
    } else {
      min_cpu = 4;
    }
  }
  return min_cpu;
}

double ObServerConfig::get_sys_tenant_default_max_cpu()
{
  double max_cpu = server_cpu_quota_max;
  if (0 == max_cpu) {
    int64_t cpu_count = get_cpu_count();
    if (cpu_count < 8) {
      max_cpu = 1;
    } else if (cpu_count < 16) {
      max_cpu = 2;
    } else if (cpu_count < 32) {
      max_cpu = 3;
    } else {
      max_cpu = 4;
    }
  }
  return max_cpu;
}

ObServerMemoryConfig::ObServerMemoryConfig()
  : memory_limit_(0)
{}

ObServerMemoryConfig &ObServerMemoryConfig::get_instance()
{
  static ObServerMemoryConfig memory_config;
  return memory_config;
}

int ObServerMemoryConfig::reload_config(const ObServerConfig& server_config)
{
  int ret = OB_SUCCESS;
  int64_t memory_limit = server_config.memory_limit;
  int64_t phy_mem_size = get_phy_mem_size();
  if (0 == memory_limit) {
    memory_limit = phy_mem_size * server_config.memory_limit_percentage / 100;
  } else if (memory_limit >= phy_mem_size) {
    OB_LOG_RET(ERROR, OB_ERR_UNEXPECTED, "memory_limit is larger than phy_mem_size", K(memory_limit), K(phy_mem_size));
  }
  memory_limit_ = memory_limit;
  LOG_INFO("update observer memory config", K_(memory_limit));
  return ret;
}

void ObServerMemoryConfig::check_limit(bool ignore_error)
{
  int ret = OB_SUCCESS;
  // check unmanaged memory size
  const int64_t UNMANAGED_MEMORY_LIMIT = 2LL<<30;
  int64_t unmanaged_memory_size = get_unmanaged_memory_size();
  if (unmanaged_memory_size > UNMANAGED_MEMORY_LIMIT) {
    if (ignore_error) {
      LOG_WARN("unmanaged_memory_size is over the limit", K(unmanaged_memory_size), K(UNMANAGED_MEMORY_LIMIT));
    } else {
      LOG_ERROR("unmanaged_memory_size is over the limit", K(unmanaged_memory_size), K(UNMANAGED_MEMORY_LIMIT));
    }
  }
}

int ObServerConfig::serialize_(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  ObRecordHeader header;
  // 这里 header 的序列化方法用的是非变长序列化，不对数字做编码
  int64_t header_len = header.get_serialize_size();
  int64_t expect_data_len = get_serialize_size_() - header_len;

  char *const p_header = buf + pos;
  char *const p_data   = p_header + header_len;
  const int64_t data_pos = pos + header_len;
  int64_t saved_header_pos     = pos;
  pos += header_len;

  // data first
  if (OB_FAIL(ObCommonConfig::serialize(buf, buf_len, pos))) {
  } else if (OB_FAIL(OTC_MGR.serialize(buf, buf_len, pos))) {
  } else {
    header.magic_ = OB_CONFIG_MAGIC;
    header.header_length_ = static_cast<int16_t>(header_len);
    header.version_ = OB_CONFIG_VERSION;
    header.data_length_ = static_cast<int32_t>(pos - data_pos);
    header.data_zlength_ = header.data_length_;
    if (header.data_zlength_ != expect_data_len) {
      LOG_WARN("unexpected data size", K_(header.data_zlength),
                                          K(expect_data_len));
    } else {
      header.data_checksum_ = ob_crc64(p_data, pos - data_pos);
      header.set_header_checksum();
      ret = header.serialize(buf, buf_len, saved_header_pos);
    }
  }
  return ret;
}

int ObServerConfig::serialize(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  OB_UNIS_ENCODE(UNIS_VERSION);
  if (OB_SUCC(ret)) {
    int64_t size_nbytes = NS_::OB_SERIALIZE_SIZE_NEED_BYTES;
    int64_t pos_bak = (pos += size_nbytes);
    if (OB_FAIL(serialize_(buf, buf_len, pos))) {
      LOG_WARN("ObServerConfig serialize fail", K(ret));
    }
    int64_t serial_size = pos - pos_bak;
    int64_t tmp_pos = 0;
    if (OB_SUCC(ret)) {
      ret = NS_::encode_fixed_bytes_i64(buf + pos_bak - size_nbytes,
        size_nbytes, tmp_pos, serial_size);
    }
  }
  return ret;
}

OB_DEF_DESERIALIZE(ObServerConfig)
{
  int ret = OB_SUCCESS;
  if (data_len == 0 || pos >= data_len) {
  } else {
    // header
    ObRecordHeader header;
    int64_t header_len = header.get_serialize_size();
    const char *const p_header = buf + pos;
    const char *const p_data = p_header + header_len;
    const int64_t data_pos = pos + header_len;
    if (OB_FAIL(header.deserialize(buf, data_len, pos))) {
      LOG_ERROR("deserialize header failed", K(ret));
    } else if (OB_FAIL(header.check_header_checksum())) {
      LOG_ERROR("check header checksum failed", K(ret));
    } else if (OB_CONFIG_MAGIC != header.magic_) {
      ret = OB_INVALID_DATA;
      LOG_ERROR("check magic number failed", K_(header.magic), K(ret));
    } else if (data_len - data_pos != header.data_zlength_) {
      ret = OB_INVALID_DATA;
      LOG_ERROR("check data len failed",
                K(data_len), K(data_pos), K_(header.data_zlength), K(ret));
    } else if (OB_FAIL(header.check_payload_checksum(p_data,
                                                     data_len - data_pos))) {
      LOG_ERROR("check data checksum failed", K(ret));
    } else if (OB_FAIL(ObCommonConfig::deserialize(buf, data_len, pos))) {
      LOG_ERROR("deserialize cluster config failed", K(ret));
    } else if (OB_FAIL(OTC_MGR.deserialize(buf, data_len, pos))){
      LOG_ERROR("deserialize tenant config failed", K(ret));
    }
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObServerConfig)
{
  int64_t len = 0;
  ObRecordHeader header;
  // 1) header size
  len += header.get_serialize_size();
  // 2) cluster config size
  len += ObCommonConfig::get_serialize_size();
  // 3) tenant config size
  len += OTC_MGR.get_serialize_size();
  return len;
}

} // end of namespace common
namespace obrpc {
int64_t get_max_rpc_packet_size()
{
  return GCONF._max_rpc_packet_size;
}

int64_t get_stream_rpc_max_wait_timeout(int64_t tenant_id)
{
  int64_t stream_rpc_max_wait_timeout = ObRpcProcessorBase::DEFAULT_WAIT_NEXT_PACKET_TIMEOUT;
  omt::ObTenantConfigGuard tenant_config(TENANT_CONF(tenant_id));
  if (OB_LIKELY(tenant_config.is_valid())) {
    stream_rpc_max_wait_timeout = tenant_config->_stream_rpc_max_wait_timeout;
  }
  return stream_rpc_max_wait_timeout;
}

bool stream_rpc_update_timeout()
{
  return true;
}

} // end of namespace obrpc
} // end of namespace oceanbase

namespace easy
{
};
