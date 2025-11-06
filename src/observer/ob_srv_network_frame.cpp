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

#define USING_LOG_PREFIX SERVER
#include "observer/ob_srv_network_frame.h"
#include "rpc/obmysql/ob_sql_nio_server.h"
#include "observer/mysql/obsm_conn_callback.h"
#include "lib/resource/ob_affinity_ctrl.h"

#include "share/ob_rpc_share.h"
#include "observer/net/ob_rpc_reverse_keepalive.h"
#include "storage/ob_locality_manager.h"
#include "rpc/obrpc/ob_local_procedure_call.h"
#include "storage/ob_locality_manager.h"

using namespace oceanbase::rpc::frame;
using namespace oceanbase::common;
using namespace oceanbase::observer;
using namespace oceanbase::share;
using namespace oceanbase::obmysql;

ObSrvNetworkFrame::ObSrvNetworkFrame(ObGlobalContext &gctx)
    : gctx_(gctx),
      xlator_(gctx),
      request_qhandler_(xlator_),
      deliver_(request_qhandler_, xlator_.get_session_handler(), gctx),
      ingress_service_(),
      SSNT_service_(),
      rpc_transport_(NULL),
      high_prio_rpc_transport_(NULL),
      mysql_transport_(NULL),
      batch_rpc_transport_(NULL),
      last_ssl_info_hash_(UINT64_MAX),
      lock_(),
      standby_fetchlog_bw_limit_(0),
      standby_fetchlog_bytes_(0),
      standby_fetchlog_time_(0)
{
  oblpc::deliver = &deliver_;
}

ObSrvNetworkFrame::~ObSrvNetworkFrame()
{
  // empty
}

static bool enable_new_sql_nio()
{
  return GCONF._enable_new_sql_nio;
}

static int update_tcp_keepalive_parameters_for_sql_nio_server(int tcp_keepalive_enabled, int64_t tcp_keepidle, int64_t tcp_keepintvl, int64_t tcp_keepcnt)
{
  int ret = OB_SUCCESS;
  if (enable_new_sql_nio()) {
    if (NULL != global_sql_nio_server) {
      tcp_keepidle = max(tcp_keepidle/1000000, 1);
      tcp_keepintvl = max(tcp_keepintvl/1000000, 1);
      global_sql_nio_server->update_tcp_keepalive_params(tcp_keepalive_enabled, tcp_keepidle, tcp_keepintvl, tcp_keepcnt);
    }
  }
  return ret;
}

int ObSrvNetworkFrame::init()
{
  int ret = OB_SUCCESS;
  const char* mysql_unix_path = "unix:run/sql.sock";
  const char* rpc_unix_path = "unix:run/rpc.sock";
  const uint32_t rpc_port = static_cast<uint32_t>(GCONF.rpc_port);
  ObNetOptions opts;
  int io_cnt = static_cast<int>(GCONF.net_thread_count);
  // make net thread count adaptive
  if (0 == io_cnt) {
    io_cnt = get_default_net_thread_count();
  }
  const int hp_io_cnt = static_cast<int>(GCONF.high_priority_net_thread_count);
  uint8_t negotiation_enable = 0;
  opts.rpc_io_cnt_ = io_cnt;
  opts.high_prio_rpc_io_cnt_ = hp_io_cnt;
  opts.mysql_io_cnt_ = io_cnt;
  if (enable_new_sql_nio()) {
    opts.mysql_io_cnt_ = 0; // if sql_nio enabled, not to create MysqlIO under the old easy framework
  }
  opts.batch_rpc_io_cnt_ = io_cnt;
  opts.use_ipv6_ = GCONF.use_ipv6;
  //TODO(tony.wzh): fix opts.tcp_keepidle  negative
  opts.tcp_user_timeout_ = static_cast<int>(GCONF.dead_socket_detection_timeout);
  opts.tcp_keepidle_     = static_cast<int>(GCONF.tcp_keepidle);
  opts.tcp_keepintvl_    = static_cast<int>(GCONF.tcp_keepintvl);
  opts.tcp_keepcnt_      = static_cast<int>(GCONF.tcp_keepcnt);

  if (GCONF.enable_tcp_keepalive) {
    opts.enable_tcp_keepalive_ = 1;
  } else {
    opts.enable_tcp_keepalive_ = 0;
  }
  LOG_INFO("io thread connection negotiation enabled!");
  negotiation_enable = 1;

  deliver_.set_host(gctx_.self_addr());

  if (OB_FAIL(request_qhandler_.init())) {
    LOG_ERROR("init rpc request qhandler fail", K(ret));

  } else if (OB_FAIL(deliver_.init())) {
    LOG_ERROR("init rpc deliverer fail", K(ret));

  } else if (OB_FAIL(reload_ssl_config())) {
    LOG_ERROR("load_ssl_config fail", K(ret));
  } else {
    // Many other modules check whether rpc_transport_ is null during startup,
    // but this structure is actually no longer used.
    // To simplify the changes, a dummy structure is set to ensure a successful startup.
    rpc_transport_ = OB_NEW(ObReqTransport, ObModIds::OB_RPC, NULL, NULL);
    share::set_obrpc_transport(rpc_transport_);
    LOG_INFO("init rpc network frame successfully",
             "ssl_client_authentication", GCONF.ssl_client_authentication.str());
  }
  return ret;
}

void ObSrvNetworkFrame::destroy()
{
  if (NULL != obmysql::global_sql_nio_server) {
    obmysql::global_sql_nio_server->destroy();
  }
}

int ObSrvNetworkFrame::start()
{
  int ret = OB_SUCCESS;
  obmysql::global_sql_nio_server =
      OB_NEW(obmysql::ObSqlNioServer, "SqlNio",
              obmysql::global_sm_conn_callback);
  if (NULL == obmysql::global_sql_nio_server) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("allocate memory for global_sql_nio_server failed", K(ret));
  } else {
    SQL_REQ_OP.set_sql_sock_processor(obmysql::global_sql_nio_server->get_sql_sock_processor());
    int sql_net_thread_count = (int)GCONF.sql_net_thread_count;
    if (sql_net_thread_count == 0) {
      if (GCONF.net_thread_count == 0) {
        sql_net_thread_count = get_default_net_thread_count();
      } else {
        sql_net_thread_count = GCONF.net_thread_count;
      }
    }
    if (GCONF._enable_numa_aware) {
      int numa_node_count = AFFINITY_CTRL.get_num_nodes();
      if (sql_net_thread_count < numa_node_count) {
        sql_net_thread_count = common::upper_align(sql_net_thread_count, numa_node_count);
        LOG_INFO("sql nio net thread count adjusted", K(sql_net_thread_count));
      }
    }
    if (OB_FAIL(obmysql::global_sql_nio_server->start(
            GCONF.mysql_port, &deliver_, sql_net_thread_count,
            GCONF._enable_numa_aware))) {
      LOG_ERROR("sql nio server start failed", K(ret));
    }
  }
  return ret;
}


int ObSrvNetworkFrame::reload_config()
{
  int ret = common::OB_SUCCESS;
  int enable_easy_keepalive = 0;
  int enable_tcp_keepalive  = 0;
  int32_t tcp_keepidle      = static_cast<int>(GCONF.tcp_keepidle);
  int32_t tcp_keepintvl     = static_cast<int>(GCONF.tcp_keepintvl);
  int32_t tcp_keepcnt       = static_cast<int>(GCONF.tcp_keepcnt);
  int32_t user_timeout      = static_cast<int>(GCONF.dead_socket_detection_timeout);

  if (GCONF._enable_easy_keepalive) {
    enable_easy_keepalive = 1;
    LOG_INFO("easy keepalive enabled.");
  } else {
    LOG_INFO("easy keepalive disabled.");
  }

  if (GCONF.enable_tcp_keepalive) {
    enable_tcp_keepalive = 1;
    LOG_INFO("tcp keepalive enabled.");
  } else {
    LOG_INFO("tcp keepalive disabled.");
  }

  if (OB_FAIL(update_tcp_keepalive_parameters_for_sql_nio_server(enable_tcp_keepalive,
                                                                        tcp_keepidle, tcp_keepintvl,
                                                                        tcp_keepcnt))) {
    LOG_WARN("Failed to set sql tcp keepalive parameters for sql nio server", K(ret));
  }
  return ret;
}

int ObSrvNetworkFrame::extract_expired_time(const char *const cert_file, int64_t &expired_time)
{
  int ret = OB_SUCCESS;
  X509 *cert = NULL;
  BIO *b = NULL;
  if (OB_ISNULL(b = BIO_new_file(cert_file, "r"))) {
    ret = OB_ERR_UNEXPECTED;
    OB_LOG(WARN, "BIO_new_file failed", K(ret), K(cert_file));
  } else if (OB_ISNULL(cert = PEM_read_bio_X509(b, NULL, 0, NULL))) {
    ret = OB_ERR_UNEXPECTED;
    OB_LOG(WARN, "PEM_read_bio_X509 failed", K(ret), K(cert_file));
  } else {
    ASN1_TIME *notAfter = X509_get_notAfter(cert);
    struct tm tm1;
    memset (&tm1, 0, sizeof (tm1));
    tm1.tm_year = (notAfter->data[ 0] - '0') * 10 + (notAfter->data[ 1] - '0') + 100;
    tm1.tm_mon  = (notAfter->data[ 2] - '0') * 10 + (notAfter->data[ 3] - '0') - 1;
    tm1.tm_mday = (notAfter->data[ 4] - '0') * 10 + (notAfter->data[ 5] - '0');
    tm1.tm_hour = (notAfter->data[ 6] - '0') * 10 + (notAfter->data[ 7] - '0');
    tm1.tm_min  = (notAfter->data[ 8] - '0') * 10 + (notAfter->data[ 9] - '0');
    tm1.tm_sec  = (notAfter->data[10] - '0') * 10 + (notAfter->data[11] - '0');
    expired_time = mktime(&tm1) * 1000000;//us
  }

  if (NULL != cert) {
    X509_free(cert);
  }
  if (NULL != b) {
    BIO_free(b);
  }
  return ret;
}

uint64_t ObSrvNetworkFrame::get_ssl_file_hash(const char *intl_file[3], const char *sm_file[5], bool &file_exist)
{
  file_exist = false;
  uint64_t hash_value = 0;
  struct stat tmp_buf[5];

  if (0 == stat(intl_file[0], tmp_buf + 0)
      && 0 == stat(intl_file[1], tmp_buf + 1)
      && 0 == stat(intl_file[2], tmp_buf + 2)) {
    file_exist = true;
    hash_value = murmurhash(&(tmp_buf[0].st_mtime), sizeof(tmp_buf[0].st_mtime), hash_value);
    hash_value = murmurhash(&(tmp_buf[1].st_mtime), sizeof(tmp_buf[1].st_mtime), hash_value);
    hash_value = murmurhash(&(tmp_buf[2].st_mtime), sizeof(tmp_buf[2].st_mtime), hash_value);
  }

  if (!file_exist) {
    if (0 == stat(sm_file[0], tmp_buf + 0)
      && 0 == stat(sm_file[1], tmp_buf + 1)
      && 0 == stat(sm_file[2], tmp_buf + 2)
      && 0 == stat(sm_file[3], tmp_buf + 3)
      && 0 == stat(sm_file[4], tmp_buf + 4)) {
      file_exist = true;
      hash_value = murmurhash(&(tmp_buf[0].st_mtime), sizeof(tmp_buf[0].st_mtime), hash_value);
      hash_value = murmurhash(&(tmp_buf[1].st_mtime), sizeof(tmp_buf[1].st_mtime), hash_value);
      hash_value = murmurhash(&(tmp_buf[2].st_mtime), sizeof(tmp_buf[2].st_mtime), hash_value);
      hash_value = murmurhash(&(tmp_buf[3].st_mtime), sizeof(tmp_buf[3].st_mtime), hash_value);
      hash_value = murmurhash(&(tmp_buf[4].st_mtime), sizeof(tmp_buf[4].st_mtime), hash_value);
    }
  } else {
    if (0 == stat(sm_file[0], tmp_buf + 0)
      && 0 == stat(sm_file[1], tmp_buf + 1)
      && 0 == stat(sm_file[2], tmp_buf + 2)
      && 0 == stat(sm_file[3], tmp_buf + 3)
      && 0 == stat(sm_file[4], tmp_buf + 4)) {
      file_exist = true;
      hash_value = murmurhash(&(tmp_buf[0].st_mtime), sizeof(tmp_buf[0].st_mtime), hash_value);
      hash_value = murmurhash(&(tmp_buf[1].st_mtime), sizeof(tmp_buf[1].st_mtime), hash_value);
      hash_value = murmurhash(&(tmp_buf[2].st_mtime), sizeof(tmp_buf[2].st_mtime), hash_value);
      hash_value = murmurhash(&(tmp_buf[3].st_mtime), sizeof(tmp_buf[3].st_mtime), hash_value);
      hash_value = murmurhash(&(tmp_buf[4].st_mtime), sizeof(tmp_buf[4].st_mtime), hash_value);
    }
  }

  return hash_value;
}

namespace oceanbase {
static int ob_add_client_CA_list(SSL_CTX *ctx, const char *cert, int cert_length)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ctx) || OB_ISNULL(cert)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid argument", KP(ctx), KP(cert));
  } else {
    STACK_OF(X509_INFO) *chain = NULL;
    X509_STORE *ca_store = NULL;
    X509_INFO *x509_info = NULL;
    BIO *cbio = NULL;
    if (OB_ISNULL(cbio = BIO_new_mem_buf((void*)cert, cert_length))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("BIO_new_mem_buf failed", K(ret));
    } else if (OB_ISNULL(chain = PEM_X509_INFO_read_bio(cbio, NULL, NULL, NULL))) {
      ret = OB_ERR_UNEXPECTED;
      int len = strlen(cert);
      common::ObString err_reason = common::ObString::make_string(ERR_reason_error_string(ERR_get_error()));
      LOG_ERROR("PEM_X509_INFO_read_bio failed", K(ret), K(cert), K(len), K(err_reason));
    } else if (OB_ISNULL(ca_store = SSL_CTX_get_cert_store(ctx))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("SSL_CTX_get_cert_store failed", K(ret));
    } else {
      for (int64_t i = 0; i < sk_X509_INFO_num(chain) && OB_SUCC(ret); i++) {
        x509_info = sk_X509_INFO_value(chain, i);
        if (OB_ISNULL(x509_info)) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("x509_info is NULL", K(i), K(ret));
        } else if (!SSL_CTX_add_client_CA(ctx, x509_info->x509)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("SSL_CTX_add_client_CA failed", K(ret), K(i));
        } else if (!X509_STORE_add_cert(ca_store, x509_info->x509)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("X509_STORE_add_cert failed", K(ret), K(i));
        }
      }
    }
    if (NULL != cbio) {
      BIO_free(cbio);
    }
    if (NULL != chain) {
      sk_X509_INFO_pop_free(chain, X509_INFO_free);
    }
  }
  return ret;
}

}

int ObSrvNetworkFrame::reload_ssl_config()
{
  int ret = common::OB_SUCCESS;
  if (GCONF.ssl_client_authentication) {
    ObString invited_nodes(GCONF._ob_ssl_invited_nodes.str());

    ObString ssl_config(GCONF.ssl_external_kms_info.str());
    bool file_exist = false;
    const char *intl_file[3] = {OB_SSL_CA_FILE, OB_SSL_CERT_FILE, OB_SSL_KEY_FILE};
    const char *sm_file[5] = {OB_SSL_CA_FILE, OB_SSL_SM_SIGN_CERT_FILE, OB_SSL_SM_SIGN_KEY_FILE, OB_SSL_SM_ENC_CERT_FILE,
    OB_SSL_SM_ENC_KEY_FILE};
    const uint64_t file_or_kms_hash_value = ssl_config.empty()
        ? get_ssl_file_hash(intl_file, sm_file, file_exist)
        : ssl_config.hash();
    const uint64_t sys_table_cerfificate_hash = get_root_certificate_table_hash();
    uint64_t new_hash_value = common::murmurhash(&sys_table_cerfificate_hash,
                                                sizeof(sys_table_cerfificate_hash),
                                                file_or_kms_hash_value);
    if (ssl_config.empty() && !file_exist) {
      ret = OB_INVALID_CONFIG;
      LOG_WARN("ssl file not available", K(new_hash_value));
      LOG_USER_ERROR(OB_INVALID_CONFIG, "ssl file not available");
    } else if (OB_ISNULL(gctx_.locality_manager_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("locality manager should not be null", K(ret), KP(gctx_.locality_manager_));
    } else if (FALSE_IT(gctx_.locality_manager_->set_ssl_invited_nodes(invited_nodes))) {
    } else if (last_ssl_info_hash_ == new_hash_value) {
      LOG_INFO("no need reload_ssl_config", K(new_hash_value));
    } else {
      bool use_bkmi = false;
      bool use_sm = false;
      const char *ca_cert = NULL;
      const char *public_cert = NULL;
      const char *private_key = NULL;
      const char *enc_cert  = NULL;
      const char *enc_private_key = NULL;


      ca_cert = OB_SSL_CA_FILE;
      public_cert = OB_SSL_CERT_FILE;
      private_key = OB_SSL_KEY_FILE;

      if (OB_SUCC(ret)) {
        int64_t ssl_key_expired_time = 0;
        if (OB_FAIL(extract_expired_time(OB_SSL_CERT_FILE, ssl_key_expired_time))) {
          OB_LOG(WARN, "extract_expired_time intl failed", K(ret), K(use_bkmi));
        } else {
          GCTX.ssl_key_expired_time_ =  ssl_key_expired_time;
          last_ssl_info_hash_ = new_hash_value;
          LOG_INFO("finish reload_ssl_config", K(use_bkmi), K(use_bkmi), K(use_sm),
                   "ssl_key_expired_time", GCTX.ssl_key_expired_time_, K(new_hash_value));
          if (OB_SUCC(ret)) {
            if (enable_new_sql_nio()) {
              common::ObSSLConfig ssl_config(!use_bkmi, use_sm, ca_cert, public_cert, private_key, NULL, NULL);
              if (OB_FAIL(ob_ssl_load_config(OB_SSL_CTX_ID_SQL_NIO, ssl_config))) {
                LOG_WARN("create ssl ctx failed!", K(ret));
              } else {
                LOG_INFO("create ssl ctx success!", K(use_bkmi), K(use_sm));
              }
            }
          }
        }
      }
    }
  } else {
    last_ssl_info_hash_ = UINT64_MAX;
    GCTX.ssl_key_expired_time_ =  0;
    LOG_INFO("finish reload_ssl_config, close ssl");
  }
  return ret;
}
void ObSrvNetworkFrame::wait()
{
  ingress_service_.wait();
  SSNT_service_.wait();
  obmysql::global_sql_nio_server->wait();
}

int ObSrvNetworkFrame::stop()
{
  int ret = OB_SUCCESS;
  deliver_.stop();
  return ret;
}

int ObSrvNetworkFrame::get_proxy(obrpc::ObRpcProxy &proxy)
{
  return proxy.init(rpc_transport_);
}

ObReqTransport *ObSrvNetworkFrame::get_req_transport()
{
  return rpc_transport_;
}

ObReqTransport *ObSrvNetworkFrame::get_high_prio_req_transport()
{
  return high_prio_rpc_transport_;
}

ObReqTransport *ObSrvNetworkFrame::get_batch_rpc_req_transport()
{
  return batch_rpc_transport_;
}

	
void ObSrvNetworkFrame::sql_nio_stop()
{
  if (NULL != obmysql::global_sql_nio_server) {
    obmysql::global_sql_nio_server->stop();
  }
}

int ObSrvNetworkFrame::reload_rpc_auth_method()
{
  int ret = OB_SUCCESS;
  return ret;
}

oceanbase::rootserver::ObIngressBWAllocService *ObSrvNetworkFrame::get_ingress_service()
{
  return &ingress_service_;
}
oceanbase::rootserver::ObSSNTAllocService *ObSrvNetworkFrame::get_SSNT_service()
{
  return &SSNT_service_;
}
int ObSrvNetworkFrame::net_endpoint_register(const ObNetEndpointKey &endpoint_key, int64_t expire_time)
{
  int ret = OB_SUCCESS;
  const uint64_t tenant_id = MTL_ID();
  if (!is_sys_tenant(tenant_id)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("endpoint register is only valid in sys tenant", K(ret), K(endpoint_key));
  } else if (ingress_service_.is_leader() && OB_FAIL(ingress_service_.register_endpoint(endpoint_key, expire_time))) {
    LOG_WARN("net endpoint register failed", K(ret), K(endpoint_key));
  }
  return ret;
}

int ObSrvNetworkFrame::net_endpoint_predict_ingress(const ObNetEndpointKey &endpoint_key, int64_t &predicted_bw)
{
  int ret = OB_NOT_SUPPORTED;
  LOG_WARN("net endpoint predict ingress is not supported");
  return ret;
}

int ObSrvNetworkFrame::net_endpoint_set_ingress(const ObNetEndpointKey &endpoint_key, int64_t assigned_bw)
{
  int ret = OB_NOT_SUPPORTED;
  LOG_WARN("net endpoint set ingress is not supported");
  return ret;
}

// share storage net throt
int ObSrvNetworkFrame::shared_storage_net_throt_register(const ObSSNTEndpointArg &endpoint_storage_infos)
{
  int ret = OB_SUCCESS;
  const uint64_t tenant_id = MTL_ID();
  if (OB_UNLIKELY(!is_sys_tenant(tenant_id))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("endpoint register is only valid in sys tenant", K(ret), K(endpoint_storage_infos));
  } else if (!SSNT_service_.is_leader()) {
    ret = OB_NOT_MASTER;
    LOG_WARN("endpoint register is only valid in leader", K(ret), K(endpoint_storage_infos));
  } else if (OB_FAIL(SSNT_service_.register_endpoint(endpoint_storage_infos))) {
    LOG_WARN("endpoint register failed", K(ret), K(endpoint_storage_infos));
  }
  return ret;
}

int ObSrvNetworkFrame::shared_storage_net_throt_predict(
    const ObSSNTEndpointArg &endpoint_storage_infos, ObSharedDeviceResourceArray &predicted_resource)
{
  int ret = OB_SUCCESS;
  struct GetNeedUsage
  {
    GetNeedUsage(
        const ObTrafficControl::ObStorageKey &key, obrpc::ObSharedDeviceResourceArray &usage, int64_t idx_begin)
        : key_(key), usage_(usage), idx_begin_(idx_begin)
    {}
    int operator()(hash::HashMapPair<ObTrafficControl::ObIORecordKey, ObTrafficControl::ObSharedDeviceIORecord> &entry)
    {
      if (key_ != entry.first.id_) {
      } else if (OB_UNLIKELY(idx_begin_ < 0)) {
      } else if (OB_UNLIKELY(idx_begin_ + ResourceType::ResourceTypeCnt > usage_.array_.count())) {
      } else {
        // key_ == entry.first.id_
        const int64_t bw_in =   entry.second.ibw_.calc();
        const int64_t bw_out =  entry.second.obw_.calc();
        const int64_t req_in =  entry.second.ips_.calc();
        const int64_t req_out = entry.second.ops_.calc();
        const int64_t tagps =   entry.second.tagps_.calc();

        usage_.array_.at(idx_begin_ + (int)obrpc::ResourceType::ops).key_ = key_;
        usage_.array_.at(idx_begin_ + (int)obrpc::ResourceType::ips).key_ = key_;
        usage_.array_.at(idx_begin_ + (int)obrpc::ResourceType::iops).key_ = key_;
        usage_.array_.at(idx_begin_ + (int)obrpc::ResourceType::obw).key_ = key_;
        usage_.array_.at(idx_begin_ + (int)obrpc::ResourceType::ibw).key_ = key_;
        usage_.array_.at(idx_begin_ + (int)obrpc::ResourceType::iobw).key_ = key_;
        usage_.array_.at(idx_begin_ + (int)obrpc::ResourceType::tag).key_ = key_;

        usage_.array_.at(idx_begin_ + (int)obrpc::ResourceType::ops).type_ = ResourceType::ops;
        usage_.array_.at(idx_begin_ + (int)obrpc::ResourceType::ips).type_ = ResourceType::ips;
        usage_.array_.at(idx_begin_ + (int)obrpc::ResourceType::iops).type_ = ResourceType::iops;
        usage_.array_.at(idx_begin_ + (int)obrpc::ResourceType::obw).type_ = ResourceType::obw;
        usage_.array_.at(idx_begin_ + (int)obrpc::ResourceType::ibw).type_ = ResourceType::ibw;
        usage_.array_.at(idx_begin_ + (int)obrpc::ResourceType::iobw).type_ = ResourceType::iobw;
        usage_.array_.at(idx_begin_ + (int)obrpc::ResourceType::tag).type_ = ResourceType::tag;

        usage_.array_.at(idx_begin_ + (int)obrpc::ResourceType::ops).value_ += req_out;
        usage_.array_.at(idx_begin_ + (int)obrpc::ResourceType::ips).value_ += req_in;
        usage_.array_.at(idx_begin_ + (int)obrpc::ResourceType::iops).value_ += req_out + req_in;
        usage_.array_.at(idx_begin_ + (int)obrpc::ResourceType::obw).value_ += bw_out;
        usage_.array_.at(idx_begin_ + (int)obrpc::ResourceType::ibw).value_ += bw_in;
        usage_.array_.at(idx_begin_ + (int)obrpc::ResourceType::iobw).value_ += bw_out + bw_in;
        usage_.array_.at(idx_begin_ + (int)obrpc::ResourceType::tag).value_ += tagps;
      }
      return OB_SUCCESS;
    }
    const ObTrafficControl::ObStorageKey &key_;
    obrpc::ObSharedDeviceResourceArray &usage_;
    int64_t idx_begin_;
  };
  const int64_t storage_key_count = endpoint_storage_infos.storage_keys_.count();
  predicted_resource.array_.reserve(storage_key_count * ResourceType::ResourceTypeCnt);
  for (int64_t i = 0; OB_SUCC(ret) && i < endpoint_storage_infos.storage_keys_.count(); ++i) {
    for (int64_t j = 0; OB_SUCC(ret) && j < ResourceType::ResourceTypeCnt; ++j) {
      if (OB_FAIL(predicted_resource.array_.push_back(ObSharedDeviceResource()))) {
        LOG_WARN("push back failed", K(predicted_resource), K(ret));
      }
    }
    int64_t idx_begin = i * ResourceType::ResourceTypeCnt;
    GetNeedUsage fn(endpoint_storage_infos.storage_keys_.at(i), predicted_resource, idx_begin);
    if (OB_FAIL(ret)) {
      LOG_WARN("push back failed", K(predicted_resource), K(ret));
    } else if (idx_begin + ResourceType::ResourceTypeCnt != predicted_resource.array_.count()) {
      LOG_WARN("predicted resource count is not match", K(predicted_resource.array_.count()), K(i), K(ret));
    } else if (OB_FAIL(OB_IO_MANAGER.get_tc().foreach_record(fn))) {
      LOG_WARN("predict failed", K(predicted_resource), K(ret));
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < predicted_resource.array_.count(); ++i) {
    uint64_t value = predicted_resource.array_[i].value_;
    const ResourceType type = predicted_resource.array_[i].type_;
    if (type == ResourceType::ops || type == ResourceType::ips || type == ResourceType::iops) {
      value = value + max((int64_t)value / 5, 100L);
    } else if (type == ResourceType::obw || type == ResourceType::ibw || type == ResourceType::iobw) {
      value = value + max((int64_t)value / 5, 1024L * 1024L);
    } else {
      value = value + max((int64_t)value / 5, 10L);
    }
    predicted_resource.array_[i].value_ = (uint64_t)(value);
  }
  if (OB_FAIL(ret)) {
    predicted_resource.array_.reuse();
  }
  return ret;
}

int ObSrvNetworkFrame::shared_storage_net_throt_set(const ObSharedDeviceResourceArray &assigned_resource)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(OB_IO_MANAGER.get_tc().set_limit_v2(assigned_resource))) {
    LOG_WARN("set failed", K(assigned_resource));
  }
  return ret;
}

uint64_t ObSrvNetworkFrame::get_root_certificate_table_hash()
{
  int ret = OB_SUCCESS;
  uint64_t hash_value = 0;
  int64_t row_count = 0;
  int64_t last_modify_time = 0;
  MTL_SWITCH(OB_SYS_TENANT_ID)
  {
    ObMySQLProxy *mysql_proxy = GCTX.sql_proxy_;
    if (OB_ISNULL(mysql_proxy)) {
      ret = OB_NOT_INIT;
      LOG_WARN("mysql proxy is not inited", K(ret));
    } else {
      int sql_len = 0;
      char sql[OB_SHORT_SQL_LENGTH];
      const char *table_name = share::OB_ALL_TRUSTED_ROOT_CERTIFICATE_TNAME;
      sql_len = snprintf(sql,
          OB_SHORT_SQL_LENGTH,
          "SELECT count(*), max(gmt_modified) "
          "FROM %s",
          table_name);
      if (sql_len >= OB_SHORT_SQL_LENGTH || sql_len <= 0) {
        ret = OB_SIZE_OVERFLOW;
        LOG_WARN("failed to format sql, buffer size not enough", K(ret));
      } else {
        SMART_VAR(ObMySQLProxy::MySQLResult, res)
        {
          common::sqlclient::ObMySQLResult *result = NULL;
          if (OB_FAIL(mysql_proxy->read(res, OB_SYS_TENANT_ID, sql))) {
            LOG_WARN("failed to read data", K(ret));
          } else if (OB_ISNULL(result = res.get_result())) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("failed to get result", K(ret));
          } else {
            while (OB_SUCC(ret) && OB_SUCC(result->next())) {
              if (OB_FAIL(result->get_int(0l, row_count))) {
                LOG_WARN("failed to get row_count", K(ret));
              } else if (OB_FAIL(result->get_timestamp("max(gmt_modified)", NULL, last_modify_time))) {
                LOG_WARN("failed to get modify_time", K(ret));
              }
            }
            if (OB_ITER_END == ret) {
              ret = OB_SUCCESS;
            }
          }
        }
      }
      if (OB_SUCC(ret)) {
        hash_value = common::murmurhash(&row_count, sizeof(row_count), last_modify_time);
      }
    }
  }
  return hash_value;
}
