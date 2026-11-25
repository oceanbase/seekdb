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

#define USING_LOG_PREFIX STORAGE

#include "standby/standby_rpc.h"
#include "standby/standby_rpc_service.h"
#include "standby/rpc_transport.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/time/ob_time_utility.h"
#include "share/ob_server_info.h"
#include "standby/ob_standby_palf_base_info.h"
#include "standby/standby_rpc_stream_util.h"
#include "standby/ob_standby_source_util.h"
#include "standby/control/standby_state_store.h"
#include "standby/restore/ob_standby_restore_reader.h"
#include "standby/standby_host.h"
#include "storage/tablet/ob_tablet_iterator.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "common/ob_version_def.h"
#include "share/rc/ob_server_runtime.h"
#include "standby/restore/ob_restore_helper_ctx.h"
#include "logservice/ob_log_handler.h"
#include <string>
#include "lib/profile/ob_trace_id.h"

using namespace oceanbase::common;
using namespace oceanbase::obcall;
using namespace oceanbase::rpc;
using namespace oceanbase::storage;


namespace oceanbase
{
namespace standby
{

namespace {
int encode_address(const common::ObAddr &address, rpc::NodeAddress &value)
{
  char ip[common::MAX_IP_ADDR_LENGTH]{};
  if (!address.is_valid() || !address.ip_to_string(ip, sizeof(ip))) {
    return OB_INVALID_ARGUMENT;
  }
  value.ip = ip;
  value.port = address.get_port();
  return OB_SUCCESS;
}

int decode_address(const rpc::NodeAddress &value, common::ObAddr &address)
{
  const std::string ip(value.ip);
  if (value.port == 0 || value.port > UINT16_MAX
      || !address.set_ip_addr(ip.c_str(), value.port)) {
    return OB_INVALID_ARGUMENT;
  }
  return OB_SUCCESS;
}

int encode_boundary_request(const StandbyPromotionBoundaryRequest &request, rpc::Control &control)
{
  int ret = OB_SUCCESS;
  control.visited.clear();
  for (int64_t i = 0; OB_SUCC(ret) && i < request.visited_.count(); ++i) {
    rpc::NodeAddress value{};
    if (OB_SUCC(ret = encode_address(request.visited_.at(i), value))) {
      control.visited.push_back(std::move(value));
    }
  }
  return ret;
}

int decode_boundary_request(const rpc::Control &control, StandbyPromotionBoundaryRequest &request)
{
  int ret = OB_SUCCESS;
  request = StandbyPromotionBoundaryRequest();
  if (control.visited.size() > MAX_PROMOTION_BOUNDARY_HOPS) return OB_INVALID_ARGUMENT;
  for (const auto &value : control.visited) {
    common::ObAddr address;
    if (OB_FAIL(decode_address(value, address)) || OB_FAIL(request.add_visited(address))) break;
  }
  return ret;
}

int encode_boundary(const StandbyPromotionBoundary &boundary, rpc::Control &control)
{
  int ret = OB_SUCCESS;
  if (!boundary.is_valid()) return OB_INVALID_ARGUMENT;
  auto &value = control.boundary;
  if (OB_FAIL(encode_address(boundary.origin_, value.origin))) return ret;
  value.cutover_scn = boundary.cutover_scn_.get_val_for_logservice();
  value.source_chain.clear();
  for (int64_t i = 0; OB_SUCC(ret) && i < boundary.source_chain_.count(); ++i) {
    const auto &hop = boundary.source_chain_.at(i);
    rpc::SourceHop entry{};
    if (OB_FAIL(encode_address(hop.relay_, entry.relay))
        || OB_FAIL(encode_address(hop.source_, entry.source))) break;
    value.source_chain.push_back(std::move(entry));
  }
  return ret;
}

int decode_boundary(const rpc::Control &control, StandbyPromotionBoundary &boundary)
{
  int ret = OB_SUCCESS;
  boundary = StandbyPromotionBoundary();
  const auto &value = control.boundary;
  if (value.source_chain.size() > MAX_PROMOTION_BOUNDARY_HOPS) return OB_INVALID_DATA;
  if (OB_FAIL(decode_address(value.origin, boundary.origin_))
      || OB_FAIL(boundary.cutover_scn_.convert_for_logservice(value.cutover_scn))) return ret;
  for (const auto &hop : value.source_chain) {
    common::ObAddr relay, source;
    if (OB_FAIL(decode_address(hop.relay, relay)) || OB_FAIL(decode_address(hop.source, source))
        || OB_FAIL(boundary.add_source_hop(relay, source))) break;
  }
  if (OB_SUCC(ret) && !boundary.is_valid()) ret = OB_INVALID_DATA;
  return ret;
}
} // namespace

class StandbyRpcService final : public rpc::Service
{
public:
  StandbyRpcService(
      const StandbyConfig &config,
      StandbyStateStore &state_store,
      IStandbyHost &host)
    : promotion_node_id_(config.promotion_node_id_),
      io_timeout_ms_(config.io_timeout_ms_),
      rpc_tls_enabled_(config.rpc_tls_enabled_),
      state_store_(state_store),
      host_(host)
  {}
  virtual ~StandbyRpcService() {}

  int fetch_ls_view(const rpc::Message* request,
      const rpc::Writer* writer) const;

  int fetch_tablet_info(const rpc::Message* request,
      const rpc::Writer* writer) const;

  int fetch_tablet_sstable_info(const rpc::Message* request,
      const rpc::Writer* writer) const;

  int fetch_sstable_macro_info(const rpc::Message* request,
      const rpc::Writer* writer) const;

  int fetch_macro_block(const rpc::Message* request,
      const rpc::Writer* writer) const;

  int get_ls_view_tablet_count(const rpc::Message* request,
      rpc::Message* response) const;

  int check_restore_precondition(const rpc::Message* request,
      rpc::Message* response) const;

  int fetch_standby_palf_base_info(const rpc::Message* request,
      rpc::Message* response) const;

  int fetch_log(const rpc::Message* request,
      const rpc::Writer* writer) const;

  int get_promotion_boundary(const rpc::Message* request,
      rpc::Message* response) const;

  int32_t dispatch(rpc::Method method, rpc::Frame frame, const rpc::Trace &trace, const rpc::Writer &writer) const override {
    common::ObCurTraceId::TraceId id;
    const std::string trace_text(trace.id);
    id.set(trace_text.c_str());
    common::ObTraceIdGuard trace_guard(id);
    rpc::Message request(std::move(frame));
    const auto *self = this;
    switch (method) {
      case rpc::Method::fetch_ls_view: return self->fetch_ls_view(&request, &writer);
      case rpc::Method::fetch_tablet_info: return self->fetch_tablet_info(&request, &writer);
      case rpc::Method::fetch_tablet_sstable_info: return self->fetch_tablet_sstable_info(&request, &writer);
      case rpc::Method::fetch_sstable_macro_info: return self->fetch_sstable_macro_info(&request, &writer);
      case rpc::Method::fetch_macro_block: return self->fetch_macro_block(&request, &writer);
      case rpc::Method::get_ls_view_tablet_count: {
        rpc::Message response;
        const int error = self->get_ls_view_tablet_count(&request, &response);
        if (error != 0) return error;
        return writer.write(response.take_frame()) ? 0 : OB_CANCELED;
      }
      case rpc::Method::check_restore_precondition: {
        rpc::Message response;
        const int error = self->check_restore_precondition(&request, &response);
        if (error != 0) return error;
        return writer.write(response.take_frame()) ? 0 : OB_CANCELED;
      }
      case rpc::Method::fetch_standby_palf_base_info: {
        rpc::Message response;
        const int error = self->fetch_standby_palf_base_info(&request, &response);
        if (error != 0) return error;
        return writer.write(response.take_frame()) ? 0 : OB_CANCELED;
      }
      case rpc::Method::fetch_log: return self->fetch_log(&request, &writer);
      case rpc::Method::get_promotion_boundary: {
        rpc::Message response;
        const int error = self->get_promotion_boundary(&request, &response);
        if (error != 0) return error;
        return writer.write(response.take_frame()) ? 0 : OB_CANCELED;
      }
      default: return OB_NOT_SUPPORTED;
    }
  }

private:
  common::ObAddr promotion_node_id_;
  int64_t io_timeout_ms_;
  bool rpc_tls_enabled_;
  StandbyStateStore &state_store_;
  IStandbyHost &host_;
};

int create_and_register_standby_rpc_service(
    rpc::RpcServer &server, const StandbyConfig &config,
    StandbyStateStore &state_store, IStandbyHost &host)
{
  return server.register_service(std::make_shared<StandbyRpcService>(config, state_store, host));
}

ObStandbyLSViewTabletCountResult::ObStandbyLSViewTabletCountResult()
  : tablet_count_(0)
{
}


ObStandbyLSViewMeta::ObStandbyLSViewMeta()
  : ls_meta_(), physical_checkpoint_scn_()
{
}

OB_SERIALIZE_MEMBER(ObStandbyLSViewMeta, ls_meta_, physical_checkpoint_scn_);

StandbyPromotionBoundary::StandbyPromotionBoundary()
  : origin_(), cutover_scn_(), source_chain_()
{
}

bool StandbyPromotionBoundary::is_valid() const
{
  bool valid = origin_.is_valid()
      && cutover_scn_.is_valid()
      && source_chain_.count() <= MAX_PROMOTION_BOUNDARY_HOPS;
  for (int64_t i = 0; valid && i < source_chain_.count(); ++i) {
    const SourceHop &hop = source_chain_.at(i);
    // A configured source is a routing endpoint. It may be an alias of the
    // callee's advertised address, so it cannot be used as node identity.
    valid = hop.is_valid();
    for (int64_t j = 0; valid && j < i; ++j) {
      valid = hop.relay_ != source_chain_.at(j).relay_;
    }
  }
  return valid;
}

bool StandbyPromotionBoundary::is_same_as(
    const StandbyPromotionBoundary &other) const
{
  bool same = origin_ == other.origin_
      && cutover_scn_ == other.cutover_scn_
      && source_chain_.count() == other.source_chain_.count();
  for (int64_t i = 0; same && i < source_chain_.count(); ++i) {
    same = source_chain_.at(i).is_same_as(other.source_chain_.at(i));
  }
  return same;
}

int StandbyPromotionBoundary::add_source_hop(
    const common::ObAddr &relay,
    const common::ObAddr &source)
{
  int ret = OB_SUCCESS;
  const SourceHop hop(relay, source);
  if (!is_valid() || !hop.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (source_chain_.count() >= MAX_PROMOTION_BOUNDARY_HOPS) {
    ret = OB_SIZE_OVERFLOW;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < source_chain_.count(); ++i) {
      if (relay == source_chain_.at(i).relay_) {
        ret = OB_INVALID_ARGUMENT;
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(source_chain_.push_back(hop))) {
      LOG_WARN("failed to append promotion source hop", K(ret), K(hop));
    }
  }
  return ret;
}


bool StandbyPromotionBoundaryRequest::is_valid() const
{
  bool valid = visited_.count() <= MAX_PROMOTION_BOUNDARY_HOPS;
  for (int64_t i = 0; valid && i < visited_.count(); ++i) {
    valid = visited_.at(i).is_valid();
    for (int64_t j = 0; valid && j < i; ++j) {
      valid = visited_.at(i) != visited_.at(j);
    }
  }
  return valid;
}

bool StandbyPromotionBoundaryRequest::contains(const common::ObAddr &addr) const
{
  bool found = false;
  for (int64_t i = 0; !found && i < visited_.count(); ++i) {
    found = visited_.at(i) == addr;
  }
  return found;
}

int StandbyPromotionBoundaryRequest::add_visited(const common::ObAddr &addr)
{
  int ret = OB_SUCCESS;
  if (!addr.is_valid() || contains(addr)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (visited_.count() >= MAX_PROMOTION_BOUNDARY_HOPS) {
    ret = OB_SIZE_OVERFLOW;
  } else if (OB_FAIL(visited_.push_back(addr))) {
    LOG_WARN("failed to extend promotion boundary path", K(ret), K(addr), K_(visited));
  }
  return ret;
}


int StandbyRpcService::fetch_ls_view(
        const rpc::Message* request,
    const rpc::Writer* writer) const
{
  int ret = OB_SUCCESS;
  const int64_t start_ts = ObTimeUtil::current_time();
  share::ObLSID ls_id(share::ObLSID::SYS_LS_ID);
  UNUSED(request);
  SERVER_MODULE_SCOPE {
    ObLSService *ls_service = nullptr;
    ObLS *ls = nullptr;
    bool ls_meta_sent = false;
    int64_t total_tablet_count = 0;
    LOG_INFO("start to fetch ls view", K(ls_id));
    auto fill_ls_meta_f = [&writer, &ls_meta_sent](const ObStandbyLSViewMeta &ls_view_meta)->int {
      int ret = OB_SUCCESS;
      if (writer->cancelled()) {
        ret = OB_CANCELED;
        LOG_WARN("client cancelled fetch_ls_view request", K(ret));
      } else {
        rpc::Message response;
        response.set_entry_type(0);
        if (OB_FAIL(serialize_message(ls_view_meta, &response))) {
          LOG_ERROR("failed to serialize standby ls view meta", K(ret));
        } else if (!writer->write(response.take_frame())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_ERROR("failed to write ls meta package to stream", K(ret));
        } else {
          ls_meta_sent = true;
        }
      }
      return ret;
    };

    auto fill_tablet_meta_f = [&writer, &total_tablet_count]
        (const obcall::ObCopyTabletInfo &tablet_info, const ObTabletHandle &tablet_handle)->int {
      int ret = OB_SUCCESS;
      UNUSED(tablet_handle);
      if (writer->cancelled()) {
        ret = OB_CANCELED;
        LOG_WARN("client cancelled fetch_ls_view request", K(ret));
      } else {
        rpc::Message response;
        response.set_entry_type(1);
        if (OB_FAIL(serialize_message(tablet_info, &response))) {
          LOG_ERROR("failed to serialize ObCopyTabletInfo", K(ret), K(tablet_info));
        } else if (!writer->write(response.take_frame())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_ERROR("failed to write tablet info to stream", K(ret), K(tablet_info));
        } else {
          ++total_tablet_count;
          LOG_INFO("fetch_ls_view batch sent", K(total_tablet_count));
        }
      }
      return ret;
    };

    LOG_INFO("start to fetch ls view", K(ls_id));
    if (OB_ISNULL(ls_service = share::server_service<storage::ObLSService>())) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "ls service should not be null", K(ret), KP(ls_service));
    } else if (OB_FAIL(ls_service->get_ls(ls))) {
      LOG_WARN("failed to get log stream", K(ret), K(ls_id));
    } else if (OB_ISNULL(ls)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("log stream should not be NULL", K(ret), KP(ls), K(ls_id));
    } else {
      ObStandbyLSViewMeta ls_view_meta;
      ObLSTabletIterator tablet_iter(ObMDSGetTabletMode::READ_WITHOUT_CHECK);
      if (OB_FAIL(ls->get_physical_restore_base(
              ls_view_meta.ls_meta_, ls_view_meta.physical_checkpoint_scn_))) {
        LOG_WARN("failed to get physical restore base", K(ret), K(ls_id));
      } else if (!ls_view_meta.is_valid()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid standby ls view meta", K(ret), K(ls_view_meta));
      } else if (OB_FAIL(fill_ls_meta_f(ls_view_meta))) {
        LOG_WARN("failed to stream ls meta", K(ret), K(ls_id));
      } else if (OB_FAIL(ls->build_tablet_iter(tablet_iter))) {
        LOG_WARN("failed to build tablet iterator", K(ret), K(ls_id));
      }

      while (OB_SUCC(ret)) {
        ObTabletHandle tablet_handle;
        ObTablet *tablet = nullptr;
        obcall::ObCopyTabletInfo tablet_info;
        if (OB_FAIL(tablet_iter.get_next_tablet(tablet_handle))) {
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
          } else {
            LOG_WARN("failed to iterate source tablets", K(ret), K(ls_id));
          }
          break;
        } else if (OB_ISNULL(tablet = tablet_handle.get_obj())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("source tablet is null", K(ret), K(tablet_handle));
        } else if (FALSE_IT(tablet_info.tablet_id_ = tablet->get_tablet_meta().tablet_id_)) {
        } else if (FALSE_IT(tablet_info.status_ = ObCopyTabletStatus::TABLET_EXIST)) {
        } else if (FALSE_IT(tablet_info.version_ = DATA_CURRENT_VERSION)) {
        } else if (OB_FAIL(tablet_info.param_.build_from_tablet(*tablet))) {
          LOG_WARN("failed to build source tablet meta", K(ret), KPC(tablet));
        } else if (OB_FAIL(fill_tablet_meta_f(tablet_info, tablet_handle))) {
          LOG_WARN("failed to stream source tablet meta", K(ret), K(tablet_info));
        }
      }
    }

    if (OB_SUCC(ret) && !ls_meta_sent) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("ls meta is not sent in fetch_ls_view stream", K(ret), K(ls_id));
    }
    if (OB_SUCC(ret)) {
      LOG_INFO("fetch_ls_view stream finished", K(ls_id), K(total_tablet_count),
          "cost_ts", ObTimeUtil::current_time() - start_ts);
    }
  }
  LOG_INFO("fetch_ls_view stream finished", K(ls_id), "cost_ts", ObTimeUtil::current_time() - start_ts);

  return ret;
}

int StandbyRpcService::get_ls_view_tablet_count(
        const rpc::Message* request,
    rpc::Message* response) const
{
  int ret = OB_SUCCESS;
  UNUSED(request);
  ObStandbyLSViewTabletCountResult result;

  SERVER_MODULE_SCOPE {
    ObLS *ls = nullptr;
    ObLSVTInfo ls_info;
    share::ObLSID ls_id(share::ObLSID::SYS_LS_ID);
    ObLSService *ls_service = share::server_service<ObLSService>();
    if (OB_ISNULL(ls_service)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ls service should not be null", K(ret));
    } else if (OB_FAIL(ls_service->get_ls(ls))) {
      LOG_WARN("failed to get ls", K(ret), K(ls_id));
    } else if (OB_ISNULL(ls)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ls is null", K(ret), K(ls_id));
    } else if (OB_FAIL(ls->get_ls_info(ls_info))) {
      LOG_WARN("failed to get ls info", K(ret), K(ls_id));
    } else {
      result.tablet_count_ = ls_info.tablet_count_;
    }
  }

  if (OB_SUCC(ret)) {
    response->control().tablet_count = result.tablet_count_;
    share::ObLSID ls_id(share::ObLSID::SYS_LS_ID);
    LOG_INFO("get_ls_view_tablet_count RPC handled successfully", K(ls_id), K(result.tablet_count_));
  }

  return ret;
}

int StandbyRpcService::fetch_tablet_info(
        const rpc::Message* request,
    const rpc::Writer* writer) const
{
  int ret = OB_SUCCESS;

  ObCopyTabletInfoArg arg;

  if (OB_FAIL(deserialize_message(*request, arg))) {
    LOG_WARN("failed to deserialize ObCopyTabletInfoArg", K(ret));
  }

  SERVER_MODULE_SCOPE {
    ObLSService *ls_service = nullptr;
    ObLS *ls = nullptr;
    ObCopyTabletInfoObProducer producer;
    if (OB_ISNULL(ls_service = share::server_service<ObLSService>())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ls service should not be null", K(ret));
    } else if (OB_FAIL(ls_service->get_ls(ls))) {
      LOG_WARN("failed to get log stream", K(ret), K(arg));
    } else if (OB_ISNULL(ls)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("log stream should not be NULL", K(ret), KP(ls), K(arg));
    } else if (OB_FAIL(producer.init(arg.ls_id_, arg.tablet_id_list_))) {
      LOG_WARN("failed to init copy tablet info producer", K(ret), K(arg));
    } else {
      ObCopyTabletInfo tablet_info;
      while (OB_SUCC(ret)) {
        tablet_info.reset();

        if (writer->cancelled()) {
          ret = OB_CANCELED;
          LOG_WARN("client cancelled the request", K(ret));
          break;
        }

        if (OB_FAIL(producer.get_next_tablet_info(tablet_info))) {
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
            break;
          } else {
            STORAGE_LOG(WARN, "failed to get next tablet meta info", K(ret));
          }
        }

        if (OB_SUCC(ret)) {
          rpc::Message response;
          if (OB_FAIL(serialize_message(tablet_info, &response))) {
            LOG_WARN("failed to serialize ObCopyTabletInfo", K(ret));
          } else {
            if (!writer->write(response.take_frame())) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("failed to write tablet info to stream", K(ret));
            }
          }
        }
      }
    }
  }

  if (OB_FAIL(ret)) {
    LOG_WARN("fetch_tablet_info stream failed", K(ret), K(arg));
  }

  return ret;
}

int StandbyRpcService::fetch_tablet_sstable_info(
        const rpc::Message* request,
    const rpc::Writer* writer) const
{
  int ret = OB_SUCCESS;

  ObCopyTabletsSSTableInfoArg arg;
  const int64_t start_ts = ObTimeUtil::current_time();
  if (OB_FAIL(deserialize_message(*request, arg))) {
    LOG_WARN("failed to deserialize ObCopyTabletsSSTableInfoArg", K(ret));
  } else {
    SERVER_MODULE_SCOPE {
      ObCopyTabletsSSTableInfoObProducer tablets_producer;
      ObLSService *ls_service = nullptr;
      obcall::ObCopyTabletSSTableInfoArg tablet_arg;
      ObLS *ls = nullptr;
      if (OB_FAIL(tablets_producer.init(arg.ls_id_, arg.tablet_sstable_info_arg_list_))) {
        LOG_WARN("failed to init copy tablets sstable info ob producer", K(ret), K(arg));
      } else if (OB_ISNULL(ls_service = share::server_service<ObLSService>())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ls service should not be null", K(ret));
      } else if (OB_FAIL(ls_service->get_ls(ls))) {
        LOG_WARN("failed to get log stream", K(ret), K(arg));
      } else if (OB_ISNULL(ls)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("log stream should not be NULL", K(ret), KP(ls), K(arg));
      }
      while (OB_SUCC(ret)) {
        tablet_arg.reset();
        if (writer->cancelled()) {
          ret = OB_CANCELED;
          LOG_WARN("client cancelled the request", K(ret));
          break;
        }
        if (OB_FAIL(tablets_producer.get_next_tablet_sstable_info(tablet_arg))) {
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
            break;
          } else {
            LOG_WARN("failed to get next tablet sstable info", K(ret), K(arg));
          }
        } else if (OB_FAIL(StandbyRpcStreamUtil::build_tablet_sstable_info(
                       tablet_arg, ls, writer))) {
          LOG_WARN("failed to build tablet sstable info", K(ret), K(tablet_arg));
        }
      }
    }
  }

  if (OB_FAIL(ret)) {
    LOG_WARN("fetch_tablet_sstable_info stream failed", K(ret), K(arg),"cost_ts", ObTimeUtil::current_time() - start_ts);
  } else {
    LOG_INFO("fetch_tablet_sstable_info stream completed", K(arg),"cost_ts", ObTimeUtil::current_time() - start_ts);
  }

  return ret;
}

int StandbyRpcService::fetch_sstable_macro_info(
        const rpc::Message* request,
    const rpc::Writer* writer) const
{
  int ret = OB_SUCCESS;

  ObCopySSTableMacroRangeInfoArg arg;
  const int64_t start_ts = ObTimeUtil::current_time();
  if (OB_FAIL(deserialize_message(*request, arg))) {
    LOG_WARN("failed to deserialize ObCopySSTableMacroRangeInfoArg", K(ret));
  } else {
    SERVER_MODULE_SCOPE {
      ObCopySSTableMacroObProducer producer;
      obcall::ObCopySSTableMacroRangeInfoHeader header;
      if (OB_FAIL(producer.init(arg.ls_id_, arg.tablet_id_,
          arg.copy_table_key_array_, arg.macro_range_max_marco_count_))) {
        LOG_WARN("failed to init copy sstable macro ob producer", K(ret), K(arg));
      } else {
        while (OB_SUCC(ret)) {
          if (writer->cancelled()) {
            ret = OB_CANCELED;
            LOG_WARN("client cancelled the request", K(ret));
            break;
          }

          header.reset();
          if (OB_FAIL(producer.get_next_sstable_macro_range_info(header))) {
            if (OB_ITER_END == ret) {
              ret = OB_SUCCESS;
              break;
            } else {
              LOG_WARN("failed to get next sstable macro range info", K(ret));
            }
          } else if (OB_FAIL(StandbyRpcStreamUtil::build_sstable_macro_info(
                         header, arg, writer))) {
            LOG_WARN("failed to build sstable macro info", K(ret), K(header));
          }
        }
      }
    }
  }

  if (OB_FAIL(ret)) {
    LOG_WARN("fetch_sstable_macro_info stream failed", K(ret), K(arg), "cost_ts", ObTimeUtil::current_time() - start_ts);
  } else {
    LOG_INFO("fetch_sstable_macro_info stream completed", K(arg), "cost_ts", ObTimeUtil::current_time() - start_ts);
  }

  return ret;
}

int StandbyRpcService::fetch_macro_block(
        const rpc::Message* request,
    const rpc::Writer* writer) const
{
  int ret = OB_SUCCESS;

  ObCopyMacroBlockRangeArg arg;
  const int64_t start_ts = ObTimeUtil::current_time();
  LOG_INFO("fetch_macro_block stream begin");
  if (OB_FAIL(deserialize_message(*request, arg))) {
    LOG_WARN("failed to deserialize ObCopyMacroBlockRangeArg", K(ret));
  } else {
    LOG_INFO("fetch_macro_block arg decoded", K(arg));
    SERVER_MODULE_SCOPE {
      ObCopyMacroBlockObProducer producer;
      blocksstable::ObBufferReader data;
      obcall::ObCopyMacroBlockHeader header;
      rpc::Message header_response;
      rpc::Message data_response;
      if (OB_FAIL(producer.init(arg.ls_id_, arg.table_key_,
          arg.copy_macro_range_info_, arg.data_version_, arg.backfill_tx_scn_, io_timeout_ms_))) {
        LOG_ERROR("failed to init copy macro block ob producer", K(ret), K(arg));
      }
      common::ObArenaAllocator header_allocator("MacroBlkHeader");
      while (OB_SUCC(ret)) {
        if (writer->cancelled()) {
          ret = OB_CANCELED;
          LOG_WARN("client cancelled the request", K(ret));
          break;
        }
        header.reset();
        if (OB_FAIL(producer.get_next_macro_block(data, header))) {
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
            break;
          } else {
            LOG_ERROR("failed to get next macro block", K(ret), K(arg));
          }
        } else {
          LOG_INFO("fetch_macro_block send one block", K(header), "data_size", data.length());
          int64_t header_size = serialization::encoded_length(header);
          char *header_buf = nullptr;
          int64_t header_pos = 0;
          header_allocator.reuse();
          if (OB_ISNULL(header_buf = static_cast<char*>(header_allocator.alloc(header_size)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("failed to alloc header buffer", K(ret), K(header_size));
          } else if (OB_FAIL(serialization::encode(header_buf, header_size, header_pos, header))) {
            LOG_WARN("failed to encode header", K(ret));
          } else if (FALSE_IT(header_response.set_buf(header_buf, header_pos))) {
          } else if (FALSE_IT(header_response.set_size(header_pos))) {
          } else if (!writer->write(header_response.take_frame())) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("failed to write header to RPC stream", K(ret));
          } else if (FALSE_IT(data_response.set_buf(data.data(), data.length()))) {
          } else if (FALSE_IT(data_response.set_size(data.length()))) {
          } else if (!writer->write(data_response.take_frame())) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("failed to write data to RPC stream", K(ret));
          }
        }
      }
    }
  }
  if (OB_FAIL(ret)) {
    LOG_WARN("fetch_macro_block stream failed", K(ret), K(arg), "cost_ts", ObTimeUtil::current_time() - start_ts);
  } else {
    LOG_INFO("fetch_macro_block stream completed", K(arg), "cost_ts", ObTimeUtil::current_time() - start_ts);
  }

  return ret;
}

int StandbyRpcService::check_restore_precondition(
        const rpc::Message* request,
    rpc::Message* response) const
{
  int ret = OB_SUCCESS;
  obcall::ObCheckRestorePreconditionResult result;

  SERVER_MODULE_SCOPE {
    ObLS *ls = nullptr;
    share::ObLSID ls_id(share::ObLSID::SYS_LS_ID);
    ObLSService *ls_service = share::server_service<ObLSService>();
    if (OB_ISNULL(ls_service)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ls service should not be null", K(ret));
    } else if (OB_FAIL(ls_service->get_ls(ls))) {
      LOG_WARN("failed to get ls", K(ret), K(ls_id));
    } else if (OB_ISNULL(ls)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ls is null", K(ret), K(ls_id));
    } else {
      ObLSTabletIterator tablet_iter(ObMDSGetTabletMode::READ_WITHOUT_CHECK);
      result.data_version_ = DATA_CURRENT_VERSION;
      if (OB_FAIL(ls->build_tablet_iter(tablet_iter))) {
        LOG_WARN("failed to build tablet iterator for restore sizing", K(ret), K(ls_id));
      }
      while (OB_SUCC(ret)) {
        ObTabletHandle tablet_handle;
        if (OB_FAIL(tablet_iter.get_next_tablet(tablet_handle))) {
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
          } else {
            LOG_WARN("failed to iterate tablet for restore sizing", K(ret), K(ls_id));
          }
          break;
        } else if (OB_ISNULL(tablet_handle.get_obj())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("tablet is null while calculating restore size", K(ret), K(ls_id));
        } else {
          const ObTabletSpaceUsage &usage = tablet_handle.get_obj()->get_tablet_meta().space_usage_;
          const int64_t data_size = std::max(
              usage.all_sstable_data_required_size_, usage.all_sstable_data_occupy_size_);
          if (data_size < 0 || usage.all_sstable_meta_size_ < 0
              || usage.tablet_clustered_meta_size_ < 0
              || data_size > INT64_MAX - usage.all_sstable_meta_size_
              || data_size + usage.all_sstable_meta_size_
                  > INT64_MAX - usage.tablet_clustered_meta_size_) {
            ret = OB_SIZE_OVERFLOW;
            LOG_WARN("invalid tablet restore size", K(ret), K(usage));
          } else {
            const int64_t tablet_size = data_size + usage.all_sstable_meta_size_
                + usage.tablet_clustered_meta_size_;
            if (result.total_tablet_size_ > INT64_MAX - tablet_size) {
              ret = OB_SIZE_OVERFLOW;
              LOG_WARN("restore size overflow", K(ret), K_(result.total_tablet_size), K(tablet_size));
            } else {
              result.total_tablet_size_ += tablet_size;
            }
          }
        }
      }
      if (OB_SUCC(ret)) {
        result.required_disk_size_ = result.total_tablet_size_;
        LOG_INFO("calculated restore disk requirement", K(ls_id), K(result.total_tablet_size_));
      }
    }
  }

  if (OB_SUCC(ret)) {
    response->control().required_disk_size = result.required_disk_size_;
    response->control().total_tablet_size = result.total_tablet_size_;
    response->control().data_version = result.data_version_;
    share::ObLSID ls_id(share::ObLSID::SYS_LS_ID);
    LOG_INFO("check_restore_precondition RPC handled successfully",
             K(ls_id), K(result.required_disk_size_), K(result.data_version_));
  }

  return ret;
}

int StandbyRpcService::fetch_standby_palf_base_info(
        const rpc::Message* request,
    rpc::Message* response) const
{
  int ret = OB_SUCCESS;
  standby::ObFetchStandbyPalfBaseInfoArg arg;
  standby::ObFetchStandbyPalfBaseInfoResult result;

  if (OB_ISNULL(request) || OB_ISNULL(response)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid fetch standby palf base info request", K(ret), KP(request), KP(response));
  } else if (OB_FAIL(deserialize_message(*request, arg))) {
    LOG_WARN("failed to deserialize ObFetchStandbyPalfBaseInfoArg", K(ret));
  } else if (OB_FAIL(standby::ObStandbyPalfBaseInfoBuilder::build(arg, result))) {
    LOG_WARN("failed to build standby palf base info", K(ret), K(arg));
  } else if (OB_FAIL(serialize_message(result, response))) {
    LOG_WARN("failed to serialize ObFetchStandbyPalfBaseInfoResult", K(ret), K(result));
  } else {
    LOG_INFO("fetch standby palf base info RPC handled successfully", K(arg), K(result));
  }

  return ret;
}

int StandbyRpcService::fetch_log(
        const rpc::Message *request,
    const rpc::Writer *writer) const
{
  int ret = OB_SUCCESS;
  static const int64_t MAX_FETCH_BYTES = 64L * 1024L * 1024L;
  if (OB_ISNULL(request) || OB_ISNULL(writer)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid fetch log request", K(ret), KP(request), KP(writer));
    return ret;
  }

  const palf::LSN start_lsn(request->start_lsn());
  int64_t sent_bytes = 0;
  if (request->max_bytes() > MAX_FETCH_BYTES) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("fetch log request is too large", K(ret), "max_bytes", request->max_bytes());
  } else if (request->max_bytes() > 0 && !start_lsn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid fetch log start lsn", K(ret), K(start_lsn));
  } else {
    SERVER_MODULE_SCOPE {
      ObLSService *ls_service = share::server_service<ObLSService>();
      ObLS *ls = nullptr;
      logservice::ObLogHandler *log_handler = nullptr;
      palf::LSN begin_lsn;
      palf::LSN end_lsn;
      palf::PalfGroupBufferIterator iterator;
      if (OB_ISNULL(ls_service)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ls service is null", K(ret));
      } else if (OB_FAIL(ls_service->get_ls(ls))) {
        LOG_WARN("failed to get log stream", K(ret));
      } else if (OB_ISNULL(ls) || OB_ISNULL(log_handler = ls->get_log_handler())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("log stream or handler is null", K(ret), KP(ls), KP(log_handler));
      } else if (request->max_bytes() > 0
                 && OB_FAIL(log_handler->get_begin_lsn(begin_lsn))) {
        LOG_WARN("failed to get source log begin lsn", K(ret), K(start_lsn));
      } else if (request->max_bytes() > 0
                 && OB_FAIL(log_handler->get_end_lsn(end_lsn))) {
        LOG_WARN("failed to get source log end lsn", K(ret), K(start_lsn));
      } else if (request->max_bytes() > 0 && start_lsn < begin_lsn) {
        ret = OB_ERR_OUT_OF_LOWER_BOUND;
        LOG_ERROR("standby requested logs older than the retained source history",
            K(ret), K(start_lsn), K(begin_lsn));
      } else if (request->max_bytes() > 0 && start_lsn > end_lsn) {
        ret = OB_ERR_OUT_OF_UPPER_BOUND;
        LOG_ERROR("standby requested logs beyond the source end",
            K(ret), K(start_lsn), K(end_lsn));
      }
      if (OB_SUCC(ret) && request->max_bytes() > 0 && start_lsn < end_lsn
          && OB_FAIL(log_handler->seek(start_lsn, iterator))) {
        LOG_ERROR("source log range contains an unreadable gap",
            K(ret), K(start_lsn), K(begin_lsn), K(end_lsn));
      }
      while (OB_SUCC(ret) && iterator.is_inited()
             && sent_bytes < static_cast<int64_t>(request->max_bytes())) {
        const char *buf = nullptr;
        int64_t size = 0;
        share::SCN log_scn;
        palf::LSN log_lsn;
        palf::LogGroupEntry group_entry;
        if (writer->cancelled()) {
          ret = OB_CANCELED;
        } else if (OB_FAIL(iterator.next())) {
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
          }
          break;
        } else if (OB_FAIL(iterator.get_entry(buf, group_entry, log_lsn))) {
          LOG_WARN("failed to read source log group", K(ret));
        } else if (FALSE_IT(size = group_entry.get_group_entry_size())) {
        } else if (FALSE_IT(log_scn = group_entry.get_scn())) {
        } else {
          rpc::Message response;
          response.set_buf(buf, size);
          response.set_size(size);
          response.set_source_lsn(log_lsn.val_);
          response.set_end_scn(log_scn.get_val_for_logservice());
          if (!writer->write(response.take_frame())) {
            ret = OB_CANCELED;
          } else {
            sent_bytes += size;
          }
        }
      }
      if (OB_SUCC(ret)) {
        share::SCN source_end_scn;
        rpc::Message response;
        if (OB_FAIL(ls->get_end_scn(source_end_scn))) {
          LOG_WARN("failed to get source log end scn", K(ret));
        } else {
          response.set_size(0);
          response.set_end_scn(source_end_scn.get_val_for_logservice());
          if (!writer->write(response.take_frame())) {
            ret = OB_CANCELED;
          }
        }
      }
    }
  }
  return ret;
}

int StandbyRpcService::get_promotion_boundary(
        const rpc::Message *request,
    rpc::Message *response) const
{
  int ret = OB_SUCCESS;
  StandbyPromotionBoundaryRequest boundary_request;
  StandbyPromotionBoundary boundary;
  if (OB_ISNULL(request) || OB_ISNULL(response)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(decode_boundary_request(request->control(), boundary_request))) {
    LOG_WARN("failed to decode promotion boundary request fields", K(ret));
  } else if (!boundary_request.is_valid()
             || boundary_request.contains(promotion_node_id_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid or cyclic promotion boundary request",
        K(ret), K(boundary_request), K_(promotion_node_id));
  } else if (OB_FAIL(boundary_request.add_visited(promotion_node_id_))) {
    LOG_WARN("promotion boundary path is too deep", K(ret), K(boundary_request));
  } else {
    SERVER_MODULE_SCOPE {
      share::ObServerInfo server_info;
      if (OB_FAIL(state_store_.load(server_info))) {
        LOG_WARN("failed to load source role transition state", K(ret));
      } else if (server_info.is_primary()) {
        ObLSService *ls_service = share::server_service<ObLSService>();
        ObLS *ls = nullptr;
        share::SCN source_end_scn;
        if (OB_ISNULL(ls_service)) {
          ret = OB_NOT_INIT;
        } else if (OB_FAIL(ls_service->get_ls(ls))) {
          LOG_WARN("failed to get source log stream", K(ret));
        } else if (OB_ISNULL(ls)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(ls->get_end_scn(source_end_scn))) {
          LOG_WARN("failed to get source end scn", K(ret));
        } else if (!server_info.has_pending_role()
                   || !server_info.get_pending_role().is_standby()
                   || !server_info.is_preparing_status()
                   || !server_info.get_cutover_scn().is_valid()
                   || source_end_scn != server_info.get_cutover_scn()) {
          ret = OB_STATE_NOT_MATCH;
          LOG_WARN("primary is not durably fenced for switchover",
              K(ret), K(server_info), K(source_end_scn));
        } else {
          boundary.origin_ = promotion_node_id_;
          boundary.cutover_scn_ = server_info.get_cutover_scn();
        }
      } else if (!server_info.is_standby()
                 || server_info.has_pending_role()) {
        ret = OB_STATE_NOT_MATCH;
        LOG_WARN("only a stable standby may relay a promotion boundary",
            K(ret), K(server_info));
      } else {
        common::ObArenaAllocator before_allocator("BoundarySrc");
        common::ObArenaAllocator after_allocator("BoundaryChk");
        common::ObString source_before;
        common::ObString source_after;
        common::ObAddr source_addr;
        common::ObAddr source_addr_after;
        StandbyRpcClient client;
        if (OB_FAIL(host_.load_log_restore_source(
            before_allocator, source_before))) {
          LOG_WARN("failed to load relay source", K(ret));
        } else if (source_before.empty()) {
          ret = OB_ENTRY_NOT_EXIST;
        } else if (OB_FAIL(StandbySourceParser::get_first_service_addr(
            source_before, source_addr))) {
          LOG_WARN("failed to parse relay source", K(ret), K(source_before));
        } else if (boundary_request.contains(source_addr)) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("promotion boundary source chain contains a cycle",
              K(ret), K(source_addr), K(boundary_request));
        } else if (OB_FAIL(client.init(
            source_addr, io_timeout_ms_ * 1000L, rpc_tls_enabled_))) {
          LOG_WARN("failed to connect promotion boundary source", K(ret), K(source_addr));
        } else if (OB_FAIL(client.get_promotion_boundary(boundary_request, boundary))) {
          LOG_WARN("failed to relay promotion boundary", K(ret), K(source_addr));
        } else if (OB_FAIL(host_.load_log_restore_source(
            after_allocator, source_after))) {
          LOG_WARN("failed to recheck relay source", K(ret));
        } else if (source_after.empty()) {
          ret = OB_ENTRY_NOT_EXIST;
          LOG_WARN("relay source was removed while resolving promotion boundary", K(ret));
        } else if (OB_FAIL(StandbySourceParser::get_first_service_addr(
            source_after, source_addr_after))) {
          LOG_WARN("failed to parse reloaded relay source", K(ret), K(source_after));
        } else if (source_before.compare(source_after) != 0
                   || source_addr != source_addr_after) {
          ret = OB_STATE_NOT_MATCH;
          LOG_WARN("relay source changed while resolving promotion boundary",
              K(ret), K(source_before), K(source_after));
        } else {
          share::ObServerInfo rechecked_server_info;
          if (OB_FAIL(state_store_.load(rechecked_server_info))) {
            LOG_WARN("failed to recheck relay role", K(ret));
          } else if (!rechecked_server_info.is_standby()
                     || rechecked_server_info.has_pending_role()) {
            ret = OB_STATE_NOT_MATCH;
            LOG_WARN("relay role changed while resolving promotion boundary",
                K(ret), K(rechecked_server_info));
          } else if (OB_FAIL(boundary.add_source_hop(
              promotion_node_id_, source_addr))) {
            LOG_WARN("failed to record stable promotion relay",
                K(ret), K_(promotion_node_id), K(source_addr));
          }
        }
      }
      if (OB_SUCC(ret) && !boundary.is_valid()) {
        ret = OB_INVALID_DATA;
        LOG_WARN("resolved invalid promotion boundary", K(ret), K(boundary));
      } else if (OB_SUCC(ret) && OB_FAIL(encode_boundary(boundary, response->control()))) {
        LOG_WARN("failed to encode promotion boundary fields", K(ret), K(boundary));
      }
    }
  }
  return ret;
}

int StandbyRpcClient::init_tablet_sstable_info_stream(
    const common::ObAddr &src_addr,
    int64_t timeout,
    bool rpc_tls_enabled,
    const obcall::ObCopyTabletsSSTableInfoArg &arg,
    restore::ObRestoreHelperSSTableInfoCtx &sstable_info_ctx)
{
  int ret = OB_SUCCESS;
  {
    StandbyRpcClient rpc_client;
    if (OB_FAIL(rpc_client.init(src_addr, timeout, rpc_tls_enabled))) {
      LOG_WARN("failed to init standby RPC client", K(ret), K(src_addr), K(timeout));
    } else if (OB_FAIL(rpc_client.create_tablet_sstable_info_stream(arg, sstable_info_ctx.sstable_info_reader_))) {
      LOG_WARN("failed to create tablet sstable info stream", K(ret), K(arg), K(src_addr));
    }
  }
  if (OB_FAIL(ret)) {
    sstable_info_ctx.sstable_info_reader_.reset();
  }

  return ret;
}

int StandbyRpcClient::init_sstable_macro_info_stream(
    const common::ObAddr &src_addr,
    int64_t timeout,
    bool rpc_tls_enabled,
    const obcall::ObCopySSTableMacroRangeInfoArg &arg,
    restore::ObRestoreHelperSSTableMacroRangeCtx &macro_range_ctx)
{
  int ret = OB_SUCCESS;
  {
    StandbyRpcClient rpc_client;
    if (OB_FAIL(rpc_client.init(src_addr, timeout, rpc_tls_enabled))) {
      LOG_WARN("failed to init standby RPC client", K(ret), K(src_addr), K(timeout));
    } else if (OB_FAIL(rpc_client.create_sstable_macro_info_stream(arg, macro_range_ctx.macro_info_reader_))) {
      LOG_WARN("failed to create sstable macro info stream", K(ret), K(arg), K(src_addr));
    }
  }
  if (OB_FAIL(ret)) {
    macro_range_ctx.macro_info_reader_.reset();
  }

  return ret;
}

int StandbyRpcClient::init_macro_block_stream(
    const common::ObAddr &src_addr,
    int64_t timeout,
    bool rpc_tls_enabled,
    const obcall::ObCopyMacroBlockRangeArg &arg,
    restore::ObRestoreHelperMacroBlockCtx &macro_block_ctx)
{
  int ret = OB_SUCCESS;
  {
    StandbyRpcClient rpc_client;
    if (OB_FAIL(rpc_client.init(src_addr, timeout, rpc_tls_enabled))) {
      LOG_WARN("failed to init standby RPC client", K(ret), K(src_addr), K(timeout));
    } else if (OB_FAIL(rpc_client.create_macro_block_stream(arg, macro_block_ctx.macro_block_reader_))) {
      LOG_WARN("failed to create macro block stream", K(ret), K(arg), K(src_addr));
    }
  }
  if (OB_FAIL(ret)) {
    macro_block_ctx.macro_block_reader_.reset();
  }

  return ret;
}

int StandbyRpcClient::init_tablet_info_stream(
    const common::ObAddr &src_addr,
    int64_t timeout,
    bool rpc_tls_enabled,
    const obcall::ObCopyTabletInfoArg &arg,
    restore::ObRestoreHelperTabletInfoCtx &tablet_info_ctx)
{
  int ret = OB_SUCCESS;
  {
    StandbyRpcClient rpc_client;
    if (OB_FAIL(rpc_client.init(src_addr, timeout, rpc_tls_enabled))) {
      LOG_WARN("failed to init standby RPC client", K(ret), K(src_addr), K(timeout));
    } else if (OB_FAIL(rpc_client.create_tablet_info_stream(arg, tablet_info_ctx.tablet_info_reader_))) {
      LOG_WARN("failed to create tablet info stream", K(ret), K(arg), K(src_addr));
    }
  }
  if (OB_FAIL(ret)) {
    tablet_info_ctx.tablet_info_reader_.reset();
  }

  return ret;
}

// ==================== StandbyRpcClient ====================
StandbyRpcClient::StandbyRpcClient()
  : is_inited_(false),
    rpc_client_()
{
}

StandbyRpcClient::~StandbyRpcClient()
{
}

int StandbyRpcClient::init(
    const common::ObAddr& addr,
    int64_t timeout,
    bool rpc_tls_enabled)
{
  int ret = OB_SUCCESS;

  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("StandbyRpcClient already inited", K(ret));
  } else if (!addr.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid addr", K(ret), K(addr));
  } else {
    ret = rpc_client_.init(addr, timeout, rpc_tls_enabled);
    if (OB_SUCC(ret)) {
      is_inited_ = true;
      LOG_INFO("StandbyRpcClient init success", K(addr), K(timeout));
    } else {
      LOG_WARN("failed to init RPC client", K(ret), K(addr));
    }
  }

  return ret;
}

int StandbyRpcClient::get_ls_view_tablet_count(ObStandbyLSViewTabletCountResult& result)
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("StandbyRpcClient not inited", K(ret));
  } else {
    rpc::Message req;
    rpc::Message res;
    auto status = rpc_client_.call(rpc::Method::get_ls_view_tablet_count, req, res);
    if (OB_FAIL(status)) {
      LOG_WARN("failed to call get_ls_view_tablet_count RPC", K(ret));
    } else {
      result.tablet_count_ = res.control().tablet_count;
      if (!result.is_valid()) ret = OB_INVALID_DATA;
    }
  }

  return ret;
}

int StandbyRpcClient::check_restore_precondition(
    obcall::ObCheckRestorePreconditionResult& result)
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("StandbyRpcClient not inited", K(ret));
  } else {
    rpc::Message req;
    rpc::Message res;
    auto status = rpc_client_.call(rpc::Method::check_restore_precondition, req, res);
    if (OB_FAIL(status)) {
      LOG_WARN("failed to call check_restore_precondition RPC", K(ret));
    } else {
      result.required_disk_size_ = res.control().required_disk_size;
      result.total_tablet_size_ = res.control().total_tablet_size;
      result.data_version_ = res.control().data_version;
    }
  }

  return ret;
}

int StandbyRpcClient::fetch_standby_palf_base_info(
    const standby::ObFetchStandbyPalfBaseInfoArg &arg,
    standby::ObFetchStandbyPalfBaseInfoResult &result)
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("StandbyRpcClient not inited", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid fetch standby palf base info arg", K(ret), K(arg));
  } else {
    rpc::Message req;
    rpc::Message res;
    if (OB_FAIL(serialize_message(arg, &req))) {
      LOG_WARN("failed to serialize ObFetchStandbyPalfBaseInfoArg", K(ret), K(arg));
    } else {
      auto status = rpc_client_.call(rpc::Method::fetch_standby_palf_base_info, req, res);
      if (OB_FAIL(status)) {
        LOG_WARN("failed to call fetch_standby_palf_base_info RPC", K(ret), K(arg));
      } else if (OB_FAIL(deserialize_message(res, result))) {
        LOG_WARN("failed to deserialize ObFetchStandbyPalfBaseInfoResult", K(ret), K(arg));
      } else if (!result.is_valid()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid fetch standby palf base info result", K(ret), K(arg), K(result));
      } else {
        LOG_INFO("fetch standby palf base info completed", K(arg), K(result));
      }
    }
  }

  return ret;
}

int StandbyRpcClient::fetch_log(
    const palf::LSN &start_lsn,
    const int64_t max_bytes,
    const std::function<int(const char *, int64_t, const palf::LSN &,
                            const share::SCN &)> &consume_log)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (!start_lsn.is_valid() || max_bytes <= 0 || !consume_log) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid fetch log argument", K(ret), K(start_lsn), K(max_bytes));
  } else {
    rpc::Message request;
    rpc::Message response;
    request.set_start_lsn(start_lsn.val_);
    request.set_max_bytes(max_bytes);
    std::unique_ptr<rpc::Reader> reader(
        rpc_client_.stream(rpc::Method::fetch_log, request));
    if (OB_ISNULL(reader)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      while (OB_SUCC(ret) && reader->read(response)) {
        share::SCN log_scn;
        const palf::LSN source_lsn(response.source_lsn());
        if (response.size() == 0) {
          // The terminal frame carries the source stream boundary.
        } else if (!source_lsn.is_valid()
                   || response.size() != static_cast<uint64_t>(response.buf().size())) {
          ret = OB_INVALID_DATA;
          LOG_WARN("invalid fetch log response", K(ret), "size", response.size(),
              "buf_size", response.buf().size(), K(source_lsn));
        } else if (OB_FAIL(log_scn.convert_for_logservice(response.end_scn()))) {
          LOG_WARN("invalid source log scn", K(ret), "source_scn", response.end_scn());
        } else if (OB_FAIL(consume_log(
            response.buf().data(), static_cast<int64_t>(response.size()), source_lsn, log_scn))) {
          LOG_WARN("failed to consume source log group", K(ret), K(source_lsn),
              K(log_scn), "size", response.size());
        }
      }
      if (OB_FAIL(ret)) {
        reader->cancel();
      }
      const int status = reader->finish();
      if (OB_SUCC(ret) && OB_FAIL(status)) {
        LOG_WARN("fetch log stream failed", K(ret));
      }
    }
  }
  return ret;
}

int StandbyRpcClient::get_promotion_boundary(
    const StandbyPromotionBoundaryRequest &boundary_request,
    StandbyPromotionBoundary &boundary)
{
  int ret = OB_SUCCESS;
  boundary = StandbyPromotionBoundary();
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (!boundary_request.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    rpc::Message request;
    rpc::Message response;
    if (OB_FAIL(encode_boundary_request(boundary_request, request.control()))) {
      LOG_WARN("failed to encode promotion boundary request fields",
          K(ret), K(boundary_request));
    } else {
      const int status = rpc_client_.call(rpc::Method::get_promotion_boundary, request, response);
      if (OB_FAIL(status)) {
        LOG_WARN("get source promotion boundary failed", K(ret));
      } else if (OB_FAIL(decode_boundary(response.control(), boundary))) {
        LOG_WARN("failed to decode source promotion boundary fields", K(ret));
      } else if (!boundary.is_valid()) {
        ret = OB_INVALID_DATA;
        LOG_WARN("source returned invalid promotion boundary", K(ret), K(boundary));
      }
    }
  }
  return ret;
}

int StandbyRpcClient::fetch_tablet_info(
    const obcall::ObCopyTabletInfoArg& arg,
    std::function<int(const obcall::ObCopyTabletInfo&)> callback)
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("StandbyRpcClient not inited", K(ret));
  } else {
    rpc::Message req;
    if (OB_FAIL(serialize_message(arg, &req))) {
      LOG_WARN("failed to serialize ObCopyTabletInfoArg", K(ret));
    } else {
      auto reader = rpc_client_.stream(rpc::Method::fetch_tablet_info, req);

      if (OB_ISNULL(reader)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to create stream reader", K(ret));
      } else {
        rpc::Message response;
        while (reader->read(response)) {
          const auto res_buf = response.buf();
          const uint64_t res_size = response.size();

          if (res_size > 0) {
            ObCopyTabletInfo tablet_info;
            if (OB_FAIL(deserialize_message(response, tablet_info))) {
              LOG_WARN("failed to deserialize ObCopyTabletInfo", K(ret), K(res_size));
              break;
            } else {
              if (OB_FAIL(callback(tablet_info))) {
                if (OB_ITER_END == ret) {
                  ret = OB_SUCCESS;
                  LOG_INFO("callback requested to stop");
                } else {
                  LOG_WARN("callback failed", K(ret));
                }
                break;
              }
            }
          }
        }

        int status = reader->finish();
        int rpc_ret = status;

        if (OB_FAIL(rpc_ret)) {
          LOG_WARN("fetch_tablet_info stream failed", K(ret));
        } else {
          LOG_INFO("fetch_tablet_info stream completed", K(arg));
        }
      }
    }
  }

  return ret;
}

int StandbyRpcClient::create_tablet_info_stream(
    const obcall::ObCopyTabletInfoArg &arg,
    std::unique_ptr<rpc::Reader> &reader)
{
  int ret = OB_SUCCESS;
  reader.reset();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("StandbyRpcClient not inited", K(ret));
  } else {
    rpc::Message req;
    if (OB_FAIL(serialize_message(arg, &req))) {
      LOG_WARN("failed to serialize ObCopyTabletInfoArg", K(ret));
    } else {
      reader = rpc_client_.stream(rpc::Method::fetch_tablet_info, req);
      if (OB_ISNULL(reader.get())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to create tablet info stream reader", K(ret));
      }
    }
  }

  return ret;
}

int StandbyRpcClient::create_ls_view_stream(
    std::unique_ptr<rpc::Reader> &reader)
{
  int ret = OB_SUCCESS;
  reader.reset();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("StandbyRpcClient not inited", K(ret));
  } else {
    rpc::Message req;
    reader = rpc_client_.stream(rpc::Method::fetch_ls_view, req);
    if (OB_ISNULL(reader.get())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to create ls view stream reader", K(ret));
    }
  }
  return ret;
}

int StandbyRpcClient::create_tablet_sstable_info_stream(
    const obcall::ObCopyTabletsSSTableInfoArg &arg,
    std::unique_ptr<rpc::Reader> &reader)
{
  int ret = OB_SUCCESS;
  reader.reset();

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("StandbyRpcClient not inited", K(ret));
  } else {
    rpc::Message req;
    if (OB_FAIL(serialize_message(arg, &req))) {
      LOG_WARN("failed to serialize ObCopyTabletsSSTableInfoArg", K(ret));
    } else {
      reader = rpc_client_.stream(rpc::Method::fetch_tablet_sstable_info, req);
      if (OB_ISNULL(reader.get())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to create tablet sstable info stream reader", K(ret));
      }
    }
  }

  return ret;
}

int StandbyRpcClient::create_sstable_macro_info_stream(
    const obcall::ObCopySSTableMacroRangeInfoArg &arg,
    std::unique_ptr<rpc::Reader> &reader)
{
  int ret = OB_SUCCESS;
  reader.reset();

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("StandbyRpcClient not inited", K(ret));
  } else {
    rpc::Message req;
    if (OB_FAIL(serialize_message(arg, &req))) {
      LOG_WARN("failed to serialize ObCopySSTableMacroRangeInfoArg", K(ret));
    } else {
      reader = rpc_client_.stream(rpc::Method::fetch_sstable_macro_info, req);
      if (OB_ISNULL(reader.get())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to create sstable macro info stream reader", K(ret));
      }
    }
  }

  return ret;
}

int StandbyRpcClient::create_macro_block_stream(
    const obcall::ObCopyMacroBlockRangeArg &arg,
    std::unique_ptr<rpc::Reader> &reader)
{
  int ret = OB_SUCCESS;
  reader.reset();

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("StandbyRpcClient not inited", K(ret));
  } else {
    rpc::Message req;
    if (OB_FAIL(serialize_message(arg, &req))) {
      LOG_WARN("failed to serialize ObCopyMacroBlockRangeArg", K(ret));
    } else {
      reader = rpc_client_.stream(rpc::Method::fetch_macro_block, req);
      if (OB_ISNULL(reader.get())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to create macro block stream reader", K(ret));
      }
    }
  }

  return ret;
}

int StandbyRpcClient::init_ls_view_stream(
    const common::ObAddr &src_addr,
    int64_t timeout,
    bool rpc_tls_enabled,
    ObLSMeta &ls_meta,
    share::SCN &physical_checkpoint_scn,
    restore::ObRestoreHelperLSViewCtx &ls_view_ctx)
{
  int ret = OB_SUCCESS;
  {
    StandbyRpcClient rpc_client;
    if (OB_FAIL(rpc_client.init(src_addr, timeout, rpc_tls_enabled))) {
      LOG_WARN("failed to init standby RPC client", K(ret), K(src_addr), K(timeout));
    } else if (OB_FAIL(rpc_client.create_ls_view_stream(ls_view_ctx.ls_view_reader_))) {
      LOG_WARN("failed to create ls view stream", K(ret), K(src_addr), K(timeout));
    }
  }
  if (OB_SUCC(ret)) {
    rpc::Message first_res;
    if (!ls_view_ctx.ls_view_reader_->read(first_res)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to read first fetch_ls_view response", K(ret), K(src_addr), K(timeout));
    } else if (0 != first_res.entry_type()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("first fetch_ls_view response is not ls meta package", K(ret),
          "entry_type", first_res.entry_type());
    } else {
      ObStandbyLSViewMeta ls_view_meta;
      if (OB_FAIL(deserialize_message(first_res, ls_view_meta))) {
        LOG_WARN("failed to deserialize standby ls view meta", K(ret));
      } else if (!ls_view_meta.is_valid()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid standby ls view meta", K(ret), K(ls_view_meta));
      } else {
        ls_meta = ls_view_meta.ls_meta_;
        physical_checkpoint_scn = ls_view_meta.physical_checkpoint_scn_;
      }
    }
    if (OB_SUCC(ret)) {
      ls_view_ctx.ls_meta_fetched_ = true;
    }
  }

  if (OB_FAIL(ret)) {
    if (ls_view_ctx.ls_view_reader_) {
      int tmp_ret = OB_SUCCESS;
      if (OB_TMP_FAIL(restore::ObRestoreHelperCtxUtil::close_reader(ls_view_ctx.ls_view_reader_))) {
        LOG_WARN("failed to close ls view reader", KR(tmp_ret));
      }
    }
    ls_view_ctx.ls_meta_fetched_ = false;
    ls_view_ctx.ls_view_reader_.reset();
  }

  return ret;
}

} // namespace standby
} // oceanbase
