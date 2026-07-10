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

#include "share/resource_limit_calculator/ob_resource_commmon.h"
#include "ob_io_manager.h"
#include "share/ob_share_util.h"  // ObShareUtil, previously hidden behind a transitive include(free within share)
#include "share/ob_server_struct.h"  // GCTX, previously hidden behind a transitive include(free within share)
#include "share/errsim_module/ob_errsim_module_interface_imp.h"
#include "share/io/io_schedule/ob_io_schedule_v2.h"
#include "lib/restore/ob_object_device.h"
#include "share/ob_io_device_helper.h"

using namespace oceanbase::obcall;
using namespace oceanbase::share;
using namespace oceanbase::lib;
using namespace oceanbase::common;

const int64_t STANDARD_IOPS_SIZE = 16 * (1<<10);

OB_SERIALIZE_MEMBER(ObTrafficControl::ObStorageKey, storage_id_, category_);
namespace oceanbase
{
namespace common
{
// for local device
int64_t get_norm_iops(const int64_t size, const double iops, const ObIOMode mode)
{
  int ret = OB_SUCCESS;
  int64_t norm_iops = 0;
  double bw = 0;
  double iops_scale = 0;
  bool is_io_ability_valid = false;
  if (iops < std::numeric_limits<double>::epsilon()) {
  } else if (FALSE_IT(bw = size * iops)) {
  } else if (mode == ObIOMode::MAX_MODE) {
    norm_iops = bw / STANDARD_IOPS_SIZE;
  } else if (FALSE_IT(ObIOCalibration::get_instance().get_iops_scale(mode, size, iops_scale, is_io_ability_valid))) {
  } else if (iops_scale < std::numeric_limits<double>::epsilon()) {
    norm_iops = bw / STANDARD_IOPS_SIZE;
    LOG_WARN("calc iops scale failed", K(ret), K(bw), K(iops), K(mode));
  } else {
    norm_iops = static_cast<int64_t>(iops / iops_scale);
  }
  return norm_iops;
}

// for local device
int64_t get_norm_bw(const int64_t size, const ObIOMode mode)
{
  int ret = OB_SUCCESS;
  int64_t norm_bw = size;
  double iops_scale = 0;
  bool is_io_ability_valid = false;
  if (mode == ObIOMode::MAX_MODE) {
  } else if (FALSE_IT(ObIOCalibration::get_instance().get_iops_scale(mode, size, iops_scale, is_io_ability_valid))) {
  } else if (iops_scale < std::numeric_limits<double>::epsilon()) {
    LOG_WARN("calc iops scale failed", K(ret), K(mode));
  } else {
    norm_bw = static_cast<int64_t>((double)STANDARD_IOPS_SIZE / iops_scale);
  }
  return max(norm_bw, 1);
}
}  // namespace common
}  // namespace oceanbase
int64_t ObTrafficControl::IORecord::calc()
{
  int64_t now = ObTimeUtility::fast_current_time();
  int64_t last_ts = ATOMIC_LOAD(&last_ts_);
  if (0 != last_ts
      && now - last_ts > 1 * 1000 * 1000
      && ATOMIC_BCAS(&last_ts_, last_ts, 0)) {
    int64_t size = 0;
    IGNORE_RETURN ATOMIC_FAA(&total_size_, size = ATOMIC_SET(&size_, 0));
    ATOMIC_STORE(&last_record_, size * 1000 * 1000 / (now - last_ts));
    ATOMIC_STORE(&last_ts_, now);
  }
  return ATOMIC_LOAD(&last_record_);
}

int ObTrafficControl::ObSharedDeviceIORecord::calc_usage(ObIORequest &req)
{
  int ret = OB_SUCCESS;
  if (req.fd_.device_handle_->is_object_device() != true) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("io request is not object device", K(req), K(ret));
  } else {
    if (req.get_mode() == ObIOMode::READ) {
      ibw_.inc(req.get_align_size());
      ips_.inc(1);
    } else if (req.get_mode() == ObIOMode::WRITE) {
      obw_.inc(req.get_align_size());
      ops_.inc(1);
    } else /* if (req.get_mode() == ObIOMode::READ) */ {
      tagps_.inc(1);
    }
  }
  return ret;
}

// when use this interface input array default size shoulde be ResourceTypeCnt
void ObTrafficControl::ObSharedDeviceIORecord::reset_total_size(ResourceUsage usages[])
{
  usages[obcall::ResourceType::ibw].type_   = obcall::ResourceType::ibw;
  usages[obcall::ResourceType::ibw].total_  = ibw_.clear();
  usages[obcall::ResourceType::obw].type_   = obcall::ResourceType::obw;
  usages[obcall::ResourceType::obw].total_  = obw_.clear();
  usages[obcall::ResourceType::ips].type_   = obcall::ResourceType::ips;
  usages[obcall::ResourceType::ips].total_  = ips_.clear();
  usages[obcall::ResourceType::ops].type_   = obcall::ResourceType::ops;
  usages[obcall::ResourceType::ops].total_  = ops_.clear();
}

ObTrafficControl::ObSharedDeviceControlV2::ObSharedDeviceControlV2()
{
  init();
}

ObTrafficControl::ObSharedDeviceControlV2::~ObSharedDeviceControlV2()
{
  destroy();
}

int ObTrafficControl::ObSharedDeviceControlV2::init()
{
  int ret = OB_SUCCESS;
  limits_[obcall::ResourceType::ops] = INT64_MAX / 2;
  limits_[obcall::ResourceType::ips] = INT64_MAX / 2;
  limits_[obcall::ResourceType::iops] = INT64_MAX;
  limits_[obcall::ResourceType::obw] = INT64_MAX / (16 * (1<<11));
  limits_[obcall::ResourceType::ibw] = INT64_MAX / (16 * (1<<11));
  limits_[obcall::ResourceType::iobw] = INT64_MAX / (16 * (1<<10));
  limits_[obcall::ResourceType::tag] = INT64_MAX;
  storage_key_  = ObStorageKey();
  return ret;
}

void ObTrafficControl::ObSharedDeviceControlV2::destroy()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(group_list_.clear())) {
    LOG_WARN("clear map failed", K(ret));
  }
}
int ObTrafficControl::ObSharedDeviceControlV2::set_storage_key(const ObTrafficControl::ObStorageKey &key)
{
  return storage_key_.assign(key);
}
int ObTrafficControl::ObSharedDeviceControlV2::add_group(const ObIOSSGrpKey &grp_key) {
  return transform_ret(group_list_.add_group(grp_key));
}


int ObTrafficControl::ObSharedDeviceControlV2::ObSDGroupList::add_group(const ObIOSSGrpKey &grp_key)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(OB_HASH_NOT_EXIST != is_group_key_exist(grp_key))) {
    LOG_WARN("repeat add the group limit ", K(ret), K(grp_key));
  } else if (OB_FAIL(grp_list_.push_back(grp_key))) {
    LOG_WARN("grp list push back failed", K(ret), K(grp_key));
  }
  LOG_INFO("add group of shared device success", K(grp_key), K(ret));
  return ret;
}

int ObTrafficControl::ObSharedDeviceControlV2::is_group_key_exist(const ObIOSSGrpKey &grp_key)
{
  return group_list_.is_group_key_exist(grp_key);
}

int64_t ObTrafficControl::ObSharedDeviceControlV2::get_limit(const obcall::ResourceType type) const
{
  return limits_[static_cast<int>(type)];
}

ObTrafficControl::ObTrafficControl()
{
  int ret = OB_SUCCESS;
  set_device_bandwidth(common::OB_DEFAULT_ETHERNET_SPEED);
  if (OB_FAIL(shared_device_map_v2_.create(7, "IO_TC_MAP_V2"))) {
    LOG_WARN("create io share device map v2 failed", K(ret));
  }
  if (OB_FAIL(io_record_map_.create(1, "IO_TC_MAP"))) {
    LOG_WARN("create io share device map failed", K(ret));
  }
}

int ObTrafficControl::calc_usage(ObIORequest &req)
{
  int ret = OB_SUCCESS;
  const ObStorageIdMod &id = ((ObObjectDevice*)(req.fd_.device_handle_))->get_storage_id_mod();
  ObIORecordKey key(ObStorageKey(id.storage_id_, id.get_category()));
  ObSharedDeviceIORecord *record = nullptr;
  if (req.fd_.device_handle_->is_object_device() != true) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("io request is not object device", K(req), K(ret));
  } else {
    int64_t io_size = req.get_align_size();
    if (OB_NOT_NULL(record = io_record_map_.get(key))) {
      // do nothing
    } else if (OB_FAIL(io_record_map_.set_refactored(key, ObSharedDeviceIORecord())) && OB_HASH_EXIST != ret) {
      LOG_WARN("set map failed", K(ret));
    } else if (OB_ISNULL(record = io_record_map_.get(key))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get index from map failed", K(ret));
    }
    if (OB_SUCC(ret)) {
      record->calc_usage(req);
    }
  }
  return ret;
}
int ObTrafficControl::transform_ret(int ret)
{
  switch (ret) {
    case 0:
      ret = OB_SUCCESS;
      break;
    case ENOENT:
      ret = OB_EAGAIN;
      break;
    case -ENOENT:
      ret = OB_EAGAIN;
      break;
    case ENOMEM:
      ret = OB_ALLOCATE_MEMORY_FAILED;
      break;
    case -ENOMEM:
      ret = OB_ALLOCATE_MEMORY_FAILED;
      break;
    default:
      LOG_WARN("unknow ret", K(ret));
      ret = OB_ERR_UNEXPECTED;
      break;
  }
  return ret;
}

void ObTrafficControl::print_server_status()
{
  inner_calc_();
  int64_t net_bw_in =  net_ibw_.calc();
  int64_t net_bw_out = net_obw_.calc();
  if (net_bw_in || net_bw_out) {
    _LOG_INFO("[IO STATUS SERVER] net_in=%ldkB/s, net_out=%ldkB/s, limit=%ldkB/s",
              net_bw_in / 1024,
              net_bw_out / 1024,
              device_bandwidth_ / 1024);
  }
}

void ObTrafficControl::print_bucket_status_V2()
{
  struct PrinterFn
  {
    struct CalFn
    {
      CalFn(const ObStorageKey &key, int64_t &bw_in, int64_t &bw_out, int64_t &req_in, int64_t &req_out, int64_t &tag)
        : key_(key), bw_in_(bw_in), bw_out_(bw_out), req_in_(req_in), req_out_(req_out), tag_(tag) {}
      int operator () (oceanbase::common::hash::HashMapPair<ObIORecordKey, ObSharedDeviceIORecord> &entry) {
        if (key_ == entry.first.id_) {
          bw_in_ +=   entry.second.ibw_.calc();
          bw_out_ +=  entry.second.obw_.calc();
          req_in_ +=  entry.second.ips_.calc();
          req_out_ += entry.second.ops_.calc();
          tag_ +=     entry.second.tagps_.calc();
        }
        return OB_SUCCESS;
      }
      const ObStorageKey &key_;
      int64_t &bw_in_;
      int64_t &bw_out_;
      int64_t &req_in_;
      int64_t &req_out_;
      int64_t &tag_;
    };
    PrinterFn(const hash::ObHashMap<ObIORecordKey, ObSharedDeviceIORecord> &map) : map_(map) {}
    int operator () (oceanbase::common::hash::HashMapPair<ObStorageKey, ObSharedDeviceControlV2*> &entry) {
      int64_t bw_in =   0;
      int64_t bw_out =  0;
      int64_t req_in =  0;
      int64_t req_out = 0;
      int64_t tag =     0;
      CalFn fn(entry.first, bw_in, bw_out, req_in, req_out, tag);
      map_.foreach_refactored(fn);
      if (OB_UNLIKELY(OB_ISNULL(entry.second))) {
      } else if (bw_in || bw_out || req_in || req_out || tag) {
        _LOG_INFO("[IO STATUS BUCKET] storage={%u, %ld, %ld}, in=[%ld / %ld]kB/s, out=[%ld / %ld]kB/s, ips=[%ld / %ld], ops=[%ld / %ld]",
                  entry.first.get_category(),
                  1UL,
                  entry.first.get_storage_id(),
                  bw_in / 1024,
                  entry.second->limits_[static_cast<int>(ResourceType::ibw)] / 1024,
                  bw_out / 1024,
                  entry.second->limits_[static_cast<int>(ResourceType::obw)] / 1024,
                  req_in,
                  entry.second->limits_[static_cast<int>(ResourceType::ips)],
                  req_out,
                  entry.second->limits_[static_cast<int>(ResourceType::ops)]);
      }
      return OB_SUCCESS;
    }
    const hash::ObHashMap<ObIORecordKey, ObSharedDeviceIORecord> &map_;
  };
  PrinterFn fn(io_record_map_);
  shared_device_map_v2_.foreach_refactored(fn);
}

void ObTrafficControl::inner_calc_()
{
  if (REACH_TIME_INTERVAL(1 * 1000 * 1000)) {
    int64_t read_bytes = 0;
    int64_t write_bytes = 0;
    net_ibw_.inc(read_bytes);
    net_obw_.inc(write_bytes);
  }
}



ObTrafficControl::ObSharedDeviceControlV2::ObSDGroupList::ObSDGroupList()
{
}
ObTrafficControl::ObSharedDeviceControlV2::ObSDGroupList::~ObSDGroupList()
{
}
int ObTrafficControl::ObSharedDeviceControlV2::ObSDGroupList::clear()
{
  grp_list_.reuse();
  return OB_SUCCESS;
}


int ObTrafficControl::ObSharedDeviceControlV2::ObSDGroupList::is_group_key_exist(const ObIOSSGrpKey &grp_key) {
  int ret = OB_SUCCESS;
  bool is_found = false;
  for (int i = 0; !is_found && i < grp_list_.count(); i++) {
    if (grp_list_.at(i) == grp_key) {
      is_found = true;
    }
  }
  if (is_found == false) {
    ret = OB_HASH_NOT_EXIST;
  }
  return ret;
}

// moved definition to the upper-layer owner cpp(omt/timer real user)

ObIOManager::ObIOManager()
  : is_inited_(false),
    is_working_(false),
    mutex_(ObLatchIds::GLOBAL_IO_CONFIG_LOCK),
    io_config_(),
    allocator_(),
    fault_detector_(io_config_)
{
}

ObIOManager::~ObIOManager()
{
  destroy();
}

ObIOManager &ObIOManager::get_instance()
{
  static ObIOManager instance;
  return instance;
}

int ObIOManager::init(const int64_t memory_limit,
                      const int32_t queue_depth,
                      const int32_t schedule_thread_count)
{
  int ret = OB_SUCCESS;
  int64_t schedule_queue_count = 0 != schedule_thread_count ? schedule_thread_count : (lib::is_mini_mode() ? 2 : 8);
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(memory_limit <= 0|| schedule_queue_count <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(memory_limit), K(schedule_queue_count));
  } else if (OB_FAIL(allocator_.init(OB_MALLOC_MIDDLE_BLOCK_SIZE, "IO_MGR", memory_limit))) {
    LOG_WARN("init io allocator failed", K(ret));
  } else if (OB_FAIL(channel_map_.create(7, "IO_CHANNEL_MAP"))) {
    LOG_WARN("create channel map failed", K(ret));
  } else if (OB_FAIL(fault_detector_.init())) {
    LOG_WARN("init io fault detector failed", K(ret));
  } else if (OB_ISNULL(server_io_manager_ = OB_NEW(ObTenantIOManager, ObMemAttr("IO_MGR")))) {
  } else if (OB_FAIL(server_io_manager_->init(ObTenantIOConfig::default_instance()))) {
    LOG_WARN("init server tenant io mgr failed", K(ret));
  } else {
    ObMemAttr attr("IO_MGR");
    allocator_.set_attr(attr);
    io_config_.set_default_value();
    is_inited_ = true;
  }
  if (OB_UNLIKELY(!is_inited_)) {
    destroy();
  }
  return ret;
}

struct DestroyChannelMapFn
{
public:
  DestroyChannelMapFn(ObIAllocator &allocator) : allocator_(allocator) {}
  int operator () (oceanbase::common::hash::HashMapPair<int64_t, ObDeviceChannel *> &entry) {
    if (nullptr != entry.second) {
      entry.second->~ObDeviceChannel();
      allocator_.free(entry.second);
    }
    return OB_SUCCESS;
  }
private:
  ObIAllocator &allocator_;
};

struct ReloadIOConfigFn
{
public:
  ReloadIOConfigFn(const ObIOConfig &conf) : conf_(conf) {}
  int operator () (oceanbase::common::hash::HashMapPair<int64_t, ObDeviceChannel *> &entry)
  {
    int ret = OB_SUCCESS;
    ObDeviceChannel *ch = entry.second;
    if (nullptr != ch) {
      if (OB_FAIL(ch->reload_config(conf_))) {
        LOG_WARN("reload device channel config failed", K(ret), KPC(ch));
      }
    }
    return ret;
  }
private:
  const ObIOConfig &conf_;
};

void ObIOManager::destroy()
{
  stop();
  fault_detector_.destroy();
  DestroyChannelMapFn destry_channel_map_fn(allocator_);
  channel_map_.foreach_refactored(destry_channel_map_fn);
  channel_map_.destroy();
  OB_DELETE(ObTenantIOManager, "IO_MGR", server_io_manager_);
  server_io_manager_ = nullptr;
  allocator_.destroy();
  is_inited_ = false;
  LOG_INFO("io manager is destroyed");
}

int ObIOManager::start()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("IO manager not init", K(ret), K(is_inited_));
  } else if (OB_FAIL(server_io_manager_->start())) {
    LOG_WARN("init server tenant io mgr start failed", K(ret));
  } else if (OB_FAIL(fault_detector_.start())) {
    LOG_WARN("start io fault detector failed", K(ret));
  } else {
    is_working_ = true;
  }
  return ret;
}

void ObIOManager::stop()
{
  is_working_ = false;
  if (OB_NOT_NULL(server_io_manager_)) {
    server_io_manager_->stop();
  }
}

void ObIOManager::wait()
{
}

bool ObIOManager::is_stopped() const
{
  return !is_working_;
}

int ObIOManager::read(const ObIOInfo &info, ObIOHandle &handle)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(aio_read(info, handle))) {
    LOG_WARN("aio read failed", K(ret), K(info));
  } else if (OB_FAIL(handle.wait())) {
    LOG_WARN("io handle wait failed", K(ret), K(info), K(info.timeout_us_));
    // io callback should be freed by caller
    handle.clear_io_callback();
  }
  return ret;
}

int ObIOManager::write(const ObIOInfo &info)
{
  int ret = OB_SUCCESS;
  ObIOHandle handle;
  if (OB_FAIL(aio_write(info, handle))) {
    LOG_WARN("aio write failed", K(ret), K(info));
  } else if (OB_FAIL(handle.wait())) {
    LOG_WARN("io handle wait failed", K(ret), K(info), K(info.timeout_us_));
    // io callback should be freed by caller
    handle.clear_io_callback();
  }
  return ret;
}

int ObIOManager::aio_read(const ObIOInfo &info, ObIOHandle &handle)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("io manager not inited", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!is_working_)) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("io manager not working", K(ret), K(is_working_));
  } else if (OB_UNLIKELY(!info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(info), K(lbt()));
  } else if (OB_FAIL(tenant_aio(info, handle))) {
    LOG_WARN("inner aio failed", K(ret), K(info));
  }
  return ret;
}

int ObIOManager::aio_write(const ObIOInfo &info, ObIOHandle &handle)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("io manager not inited", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!is_working_)) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("io manager not working", K(ret), K(is_working_));
  } else if (OB_UNLIKELY(!info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(info), K(lbt()));
  } else if (OB_FAIL(tenant_aio(info, handle))) {
    LOG_WARN("inner aio failed", K(ret), K(info));
  }
  return ret;
}

int ObIOManager::pread(ObIOInfo &info, int64_t &read_size)
{
  int ret = OB_SUCCESS;
  read_size = 0;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("io manager not inited", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!is_working_)) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("io manager not working", K(ret), K(is_working_));
  } else if (OB_UNLIKELY(!info.is_valid() || nullptr == info.buf_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(info));
  } else {
    info.flag_.set_read();
    info.flag_.set_sync();
    info.timeout_us_ = MAX_IO_WAIT_TIME_MS * 1000;
    ObIOHandle handle;
    if (OB_FAIL(tenant_aio(info, handle))) {
      LOG_WARN("do inner aio failed", K(ret), K(info));
    } else {
      while (OB_SUCC(ret) || OB_TIMEOUT == ret || OB_IO_TIMEOUT == ret) { // wait to die
        if (OB_FAIL(handle.wait(MAX_IO_WAIT_TIME_MS))) {
          if (OB_DATA_OUT_OF_RANGE != ret) {
            LOG_WARN("sync read failed", K(ret), K(info));
          }
        } else {
          break;
        }
      }
    }
    if (OB_SUCC(ret) || OB_DATA_OUT_OF_RANGE == ret) {
      read_size = handle.get_data_size();
      MEMCPY(const_cast<char *>(info.buf_), handle.get_buffer(), read_size);
    }
  }
  return ret;
}

int ObIOManager::pwrite(ObIOInfo &info, int64_t &write_size)
{
  int ret = OB_SUCCESS;
  write_size = 0;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("io manager not inited", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!is_working_)) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("io manager not working", K(ret), K(is_working_));
  } else if (OB_UNLIKELY(!info.is_valid() || nullptr == info.buf_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(info));
  } else {
    info.flag_.set_write();
    info.flag_.set_sync();
    info.timeout_us_ = MAX_IO_WAIT_TIME_MS * 1000;
    ObIOHandle handle;
    if (OB_FAIL(tenant_aio(info, handle))) {
      LOG_WARN("do inner aio failed", K(ret), K(info));
    } else {
      while (OB_SUCC(ret) || OB_TIMEOUT == ret || OB_IO_TIMEOUT == ret) { // wait to die
        if (OB_FAIL(handle.wait(MAX_IO_WAIT_TIME_MS))) {
          if (OB_DATA_OUT_OF_RANGE != ret) {
            LOG_WARN("sync write failed", K(ret), K(info));
          }
        } else {
          break;
        }
      }
    }
    if (OB_SUCC(ret) || OB_DATA_OUT_OF_RANGE == ret) {
      write_size = handle.get_data_size();
    }
  }
  return ret;
}

int ObIOManager::detect_read(const ObIOInfo &info, ObIOHandle &handle)
{
  int ret = OB_SUCCESS;
  ObRefHolder<ObTenantIOManager> tenant_holder;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("io manager not inited", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!is_working_)) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("io manager not working", K(ret), K(is_working_));
  } else if (OB_UNLIKELY(!info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(info), K(lbt()));
  } else if (OB_FAIL(get_tenant_io_manager(tenant_holder))) {
    LOG_WARN("get tenant io manager failed", K(ret));
  } else if (OB_FAIL(tenant_holder.get_ptr()->detect_aio(info, handle))) {
    LOG_WARN("tenant io manager do aio failed", K(ret), K(info), KPC(tenant_holder.get_ptr()));
  } else if (OB_FAIL(handle.wait())) {
    LOG_WARN("io handle wait failed", K(ret), K(info));
  }
  return ret;
}

int ObIOManager::tenant_aio(const ObIOInfo &info, ObIOHandle &handle)
{
  int ret = OB_SUCCESS;
  ObRefHolder<ObTenantIOManager> tenant_holder;
#ifdef ERRSIM
  const ObErrsimModuleType type = THIS_WORKER.get_module_type();
  if (is_errsim_module(type.type_)) {
    ret = OB_IO_ERROR;
    LOG_ERROR("[ERRSIM MODULE] errsim IO error", K(ret));
    return ret;
  }
#endif

  if (OB_FAIL(get_tenant_io_manager(tenant_holder))) {
    LOG_WARN("get tenant io manager failed", K(ret));
  } else if (OB_FAIL(tenant_holder.get_ptr()->inner_aio(info, handle))) {
    LOG_WARN("tenant io manager do aio failed", K(ret), K(info), KPC(tenant_holder.get_ptr()));
  }
  return ret;
}

int ObIOManager::set_io_config(const ObIOConfig &conf)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("The ObIOManager has not been inited, ", K(ret));
  } else if (OB_UNLIKELY(!conf.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument, ", K(conf), K(ret));
  } else {
    ObMutexGuard guard(mutex_);
    ReloadIOConfigFn fn(conf);
    if (OB_FAIL(channel_map_.foreach_refactored(fn))) {
      LOG_WARN("reload io config failed", K(ret));
    } else {
      io_config_ = conf;
    }
  }
  LOG_INFO("set io config for io manager, ", K(ret), K(conf));
  return ret;
}

const ObIOConfig &ObIOManager::get_io_config() const
{
  return io_config_;
}

ObIOFaultDetector &ObIOManager::get_device_health_detector()
{
  return fault_detector_;
}

int ObIOManager::get_device_health_status(ObDeviceHealthStatus &dhs, int64_t &device_abnormal_time)
{
  return fault_detector_.get_device_health_status(dhs, device_abnormal_time);
}

int ObIOManager::add_device_channel(ObIODevice *device_handle,
                                    const int64_t async_channel_thread_count,
                                    const int64_t sync_channel_thread_count,
                                    const int64_t max_io_depth)
{
  int ret = OB_SUCCESS;
  ObDeviceChannel *device_channel = nullptr;
  void *buf = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  /* object device async channel count allow set 0 */
  } else if (OB_ISNULL(device_handle) || async_channel_thread_count < 0 || sync_channel_thread_count < 0 || max_io_depth <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(device_handle), K(async_channel_thread_count), K(sync_channel_thread_count), K(max_io_depth));
  } else if (OB_ISNULL(buf = allocator_.alloc(sizeof(ObDeviceChannel)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("alloc device channel failed", K(ret));
  } else if (FALSE_IT(device_channel = new (buf) ObDeviceChannel)) {
  } else if (OB_FAIL(device_channel->init(device_handle,
                                          async_channel_thread_count,
                                          sync_channel_thread_count,
                                          max_io_depth,
                                          allocator_))) {
    LOG_WARN("init device_channel failed", K(ret), K(async_channel_thread_count), K(sync_channel_thread_count));
  } else if (OB_FAIL(channel_map_.set_refactored(reinterpret_cast<int64_t>(device_handle), device_channel))) {
    LOG_WARN("set channel map failed", K(ret), KP(device_handle));
  } else {
    LOG_INFO("add io device channel succ", KP(device_handle));
    device_channel = nullptr;
  }
  if (OB_UNLIKELY(nullptr != device_channel)) {
    device_channel->~ObDeviceChannel();
    allocator_.free(device_channel);
  }
  return ret;
}

int ObIOManager::remove_device_channel(ObIODevice *device_handle)
{
  int ret = OB_SUCCESS;
  ObDeviceChannel *device_channel = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_ISNULL(device_handle)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(device_handle));
  } else if (OB_FAIL(channel_map_.erase_refactored(reinterpret_cast<int64_t>(device_handle), &device_channel))) {
    LOG_WARN("remove from channel map failed", K(ret), KP(device_handle));
  } else if (nullptr != device_channel) {
    device_channel->~ObDeviceChannel();
    allocator_.free(device_channel);
  }
  return ret;
}

int ObIOManager::get_device_channel(const ObIORequest &req, ObDeviceChannel *&device_channel)
{
  // for now, different device_handle use same channel
  int ret = OB_SUCCESS;
  ObIODevice *device_handle = req.fd_.is_backup_block_file() ? &LOCAL_DEVICE_INSTANCE : req.fd_.device_handle_;
  device_channel = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_ISNULL(device_handle)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(device_handle));
  } else if (OB_FAIL(channel_map_.get_refactored(reinterpret_cast<int64_t>(device_handle), device_channel))) {
    LOG_WARN("get device channel failed", K(ret), KP(device_handle));
  }
  return ret;
}

int ObIOManager::refresh_tenant_io_unit_config(const ObTenantIOConfig::UnitConfig &tenant_io_unit_config)
{
  int ret = OB_SUCCESS;
  ObRefHolder<ObTenantIOManager> tenant_holder;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!true ||
                         !tenant_io_unit_config.is_valid())) { 
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tenant_io_unit_config));
  } else if (OB_FAIL(get_tenant_io_manager(tenant_holder))) {
    LOG_WARN("get tenant io manager failed", K(ret));
  } else if (OB_FAIL(tenant_holder.get_ptr()->update_basic_io_unit_config(tenant_io_unit_config))) {
    LOG_WARN("update tenant io config failed", K(ret), K(tenant_io_unit_config));
  }
  return ret;
}

int ObIOManager::refresh_tenant_io_param_config(const ObTenantIOConfig::ParamConfig &tenant_io_param_config)
{
  int ret = OB_SUCCESS;
  ObRefHolder<ObTenantIOManager> tenant_holder;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!true ||
                         !tenant_io_param_config.is_valid())) { 
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tenant_io_param_config));
  } else if (OB_FAIL(get_tenant_io_manager(tenant_holder))) {
    LOG_WARN("get tenant io manager failed", K(ret));
  } else if (OB_FAIL(tenant_holder.get_ptr()->update_basic_io_param_config(tenant_io_param_config))) {
    LOG_WARN("update tenant io config failed", K(ret), K(tenant_io_param_config));
  }
  return ret;
}

int ObIOManager::get_tenant_io_manager(ObRefHolder<ObTenantIOManager> &tenant_holder) const
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(server_io_manager_)) {
    tenant_holder.hold(server_io_manager_);
  }
  if (OB_SUCC(ret) && OB_ISNULL(tenant_holder.get_ptr())) {
    ret = OB_HASH_NOT_EXIST;
  }
  return ret;
}


// moved definition to the upper-layer owner cpp(omt/timer real user)

void ObIOManager::print_channel_status()
{
  struct PrintFn
  {
    int operator () (oceanbase::common::hash::HashMapPair<int64_t, ObDeviceChannel*> &entry) {
      if (OB_NOT_NULL(entry.second)) {
        entry.second->print_status();
      }
      return OB_SUCCESS;
    }
  };
  PrintFn fn;
  channel_map_.foreach_refactored(fn);
}

void ObIOManager::print_status()
{
  print_tenant_status();
  print_channel_status();
  tc_.print_server_status();
  tc_.print_bucket_status_V2();
}

int64_t ObIOManager::get_object_storage_io_timeout_ms() const
{
  int ret = OB_SUCCESS;
  int64_t timeout_ms = DEFAULT_OBJECT_STORAGE_IO_TIMEOUT_MS;
  ObRefHolder<ObTenantIOManager> tenant_holder;
  if (OB_UNLIKELY(!true)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tenant id", KR(ret));
  } else if (OB_FAIL(get_tenant_io_manager(tenant_holder))) {
    LOG_WARN("fail to get tenant io manager", KR(ret));
  } else if (OB_ISNULL(tenant_holder.get_ptr())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tenant holder ptr is null", KR(ret));
  } else {
    timeout_ms = tenant_holder.get_ptr()->get_object_storage_io_timeout_ms();
  }
  return timeout_ms;
}

/******************             TenantIOManager              **********************/

int ObTenantIOManager::mtl_new(ObTenantIOManager *&io_service)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  io_service = nullptr;
  if (OB_ISNULL(buf = ob_malloc(sizeof(ObTenantIOManager), ObMemAttr("IO_MGR")))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    FLOG_WARN("failed to alloc tenant io mgr", K(ret));
  } else {
    io_service = new (buf) ObTenantIOManager();
  }
  return ret;
}

int ObTenantIOManager::mtl_init(ObTenantIOManager *&io_service)
{
  int ret = OB_SUCCESS;
  
  if (OB_ISNULL(io_service)) {
    {
      ret = OB_INVALID_ARGUMENT;
    }
  } else if (OB_FAIL(io_service->init(ObTenantIOConfig::default_instance()))) {
    FLOG_WARN("mtl iit tenant io manager failed", K(1UL));
  } else {
    FLOG_INFO("mtl init tenant io manager success", K(1UL), KPC(io_service));
  }
  return ret;
}

void ObTenantIOManager::mtl_destroy(ObTenantIOManager *&io_service)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(io_service)) {
    io_service->~ObTenantIOManager();
    ob_free(io_service);
    io_service = nullptr;
    FLOG_INFO("mtl destroy tenant io manager success");
  }
}

ObTenantIOManager::ObTenantIOManager()
  : is_inited_(false),
    is_working_(false),
    ref_cnt_(0),
    io_memory_limit_(0),
    request_count_(0),
    result_count_(0),
    io_config_(),
    io_allocator_(),
    callback_mgr_(),
    io_config_lock_(ObLatchIds::TENANT_IO_CONFIG_LOCK),
    group_id_index_map_()
{

}

ObTenantIOManager::~ObTenantIOManager()
{
  destroy();
}

int ObTenantIOManager::init(const ObTenantIOConfig &io_config)
{
  int ret = OB_SUCCESS;
  const uint8_t IO_MODE_CNT = static_cast<uint8_t>(ObIOMode::MAX_MODE) + 1;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!true
        || !io_config.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(io_config));
  } else if (OB_FAIL(init_memory_pool( io_config.param_config_.memory_limit_))) {
    LOG_WARN("init tenant io memory pool failed", K(ret), K(io_config), K(io_memory_limit_), K(request_count_), K(request_count_));
  } else if (OB_FAIL(io_usage_.init(io_config.group_configs_.count() / IO_MODE_CNT))) {
    LOG_WARN("init io usage failed", K(ret), K(io_usage_), K(io_config.group_configs_.count()));
  } else if (OB_FAIL(io_sys_usage_.init(SYS_MODULE_CNT))) { // local and remote
    LOG_WARN("init io usage failed", K(ret), K(io_sys_usage_), K(SYS_MODULE_CNT), K(SYS_MODULE_CNT * 2));
  } else if (OB_FAIL(io_mem_stats_.init(SYS_MODULE_CNT , io_config.group_configs_.count() / IO_MODE_CNT))) {
    LOG_WARN("init io usage failed", K(ret), K(io_mem_stats_), K(SYS_MODULE_CNT), K(io_config.group_configs_.count()));
  } else if (OB_FAIL(init_group_index_map(io_config))) {
    LOG_WARN("init group map failed", K(ret));
  } else if (OB_FAIL(io_config_.deep_copy(io_config))) {
    LOG_WARN("copy io config failed", K(ret), K(io_config_));
  } else if(OB_FAIL(io_config_.group_configs_.reserve(16L * IO_MODE_CNT))) {
    //rerserve space for 16 groups to avoid concurrency problem
    LOG_WARN("reserve group configs failed", K(ret));
  } else if (OB_FAIL(qsched_.init(io_config))) {
    LOG_WARN("init qsched failed", K(ret), K(io_config));
  } else {
    
    inc_ref();
    is_inited_ = true;
  }
  if (OB_UNLIKELY(!is_inited_)) {
    destroy();
  }
  return ret;
}

void ObTenantIOManager::destroy()
{
  ATOMIC_STORE(&is_working_, false);

  const int64_t start_ts = ObTimeUtility::current_time();
  if (is_inited_) {
    while (1 != get_ref_cnt()) {
      if (REACH_TIME_INTERVAL(1000L * 1000L)) { //1s
        LOG_INFO("wait tenant io manager quit", K(start_ts), K(get_ref_cnt()));
      }
      ob_usleep((useconds_t)10L * 1000L); //10ms
    }
    dec_ref();
    qsched_.destroy();
  }

  int ret = OB_SUCCESS;

  callback_mgr_.destroy();
  io_memory_limit_ = 0;
  request_count_ = 0;
  result_count_ = 0;
  group_id_index_map_.destroy();
  io_allocator_.destroy();
  LOG_INFO("destroy tenant io manager success");
  
  is_inited_ = false;
}

int ObTenantIOManager::start()
{
  int ret = OB_SUCCESS;
  static const int64_t DEFAULT_QUEUE_DEPTH = 100000;
  int64_t callback_thread_count = io_config_.get_callback_thread_count();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (is_working()) {
    // do nothing
  } else if (OB_FAIL(callback_mgr_.init(callback_thread_count,
                     callback_thread_count * DEFAULT_QUEUE_DEPTH))) {
    LOG_WARN("init callback manager failed", K(ret), K(callback_thread_count));
  } else {
    is_working_ = true;
  }
  return ret;
}

void ObTenantIOManager::stop()
{
  ATOMIC_STORE(&is_working_, false);
  callback_mgr_.destroy();
}

bool ObTenantIOManager::is_working() const
{
  return ATOMIC_LOAD(&is_working_);
}

int ObTenantIOManager::calc_io_memory(const int64_t memory)
{
  int ret = OB_SUCCESS;
  int64_t memory_benchmark = memory / (1L * 1024L * 1024L * 1024L); //base ob 1G
  //1w req occupies 1.52M
  //1w result occupies 2.44M
  if (lib::is_mini_mode() && true) {
    request_count_ = 5000;
    result_count_ = 5000;
    io_memory_limit_ = 256L * 1024L * 1024L;
  } else if (memory_benchmark <= 1) {
    //1G tenant upper limit is 256MB, pre-allocate 50k requests (7.6MB) and results (12.2MB)
    request_count_ = 50000;
    result_count_ = 50000;
    io_memory_limit_ = 256L * 1024L * 1024L;
  } else if (memory_benchmark <= 4) {
    //4G tenant upper limit is 1G, pre-allocate 100k request (15.2MB) and result (24.4MB)
    request_count_ = 100000;
    result_count_ = 100000;
    io_memory_limit_ = 1024 * 1024L * 1024L;
  } else if (memory_benchmark <= 8) {
    //8G tenant upper limit is 2G, pre-allocate 200k request and result
    request_count_ = 200000;
    result_count_ = 200000;
    io_memory_limit_ = 2048L * 1024L * 1024L;
  } else {
    //unlimited, pre-allocate 300k request and result
    request_count_ = 300000;
    result_count_ = 300000;
    io_memory_limit_ = memory;
  }
  LOG_INFO("calc tenant io memory success", K(memory), K(io_memory_limit_), K(request_count_), K(request_count_));
  return ret;
}

int ObTenantIOManager::init_memory_pool(const int64_t memory)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(memory <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid io argument", K(ret), K(memory));
  } else if (OB_FAIL(calc_io_memory( memory))) {
    LOG_WARN("calc tenant io memory failed", K(ret), K(memory), K(io_memory_limit_), K(request_count_), K(request_count_));
  } else if (OB_FAIL(io_allocator_.init(io_memory_limit_))) {
    LOG_WARN("init io allocator failed", K(ret), K(io_memory_limit_));
  } else {
    LOG_INFO("init tenant io memory pool success", K(memory), K(io_memory_limit_), K(request_count_), K(request_count_));
  }
  return ret;
}

int ObTenantIOManager::update_memory_pool(const int64_t memory)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(memory <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid io argument", K(ret), K(memory));
  } else if (OB_FAIL(calc_io_memory( memory))) {
    LOG_WARN("calc tenant io memory failed", K(ret), K(memory), K(io_memory_limit_), K(request_count_), K(request_count_));
  } else if (OB_FAIL(io_allocator_.update_memory_limit(io_memory_limit_))) {
    LOG_WARN("update io memory limit failed", K(ret), K(io_memory_limit_));
  } else {
    LOG_INFO("update tenant io memory pool success", K(memory), K(io_memory_limit_), K(request_count_), K(request_count_));
  }
  //todo qilu :update three pool
  return ret;
}

int ObTenantIOManager::alloc_and_init_result(const ObIOInfo &info, ObIOResult *&io_result)
{
  int ret = OB_SUCCESS;
  io_result = nullptr;
  if (OB_FAIL(alloc_io_result(io_result))) {
    if (OB_ALLOCATE_MEMORY_FAILED == ret) {
      LOG_WARN("alloc io result failed, retry until timeout", K(ret));
      //blocking foreground thread
      ret = OB_SUCCESS;
      if (OB_FAIL(try_alloc_result_until_timeout(ObTimeUtility::current_time() + info.timeout_us_, io_result))) {
        LOG_WARN("retry alloc io result failed", K(ret));
      }
    } else {
      LOG_WARN("alloc io result failed", K(ret), KP(io_result));
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(io_result)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("io result is null", K(ret));
    } else if (OB_FAIL(io_result->basic_init())) {
      LOG_WARN("basic init io result failed", K(ret));
    }
  }

  if (OB_FAIL(ret)) {
  } else if (FALSE_IT(io_result->io_callback_ = info.callback_)) {
  } else if (OB_FAIL(io_result->init(info))) {
    LOG_WARN("init io result failed", K(ret), KPC(io_result));
  }

  if (OB_FAIL(ret) && OB_NOT_NULL(io_result)) {
    io_allocator_.free(io_result);
  }
  return ret;
}

//prepare request and result
int ObTenantIOManager::alloc_req_and_result(const ObIOInfo &info, ObIOHandle &handle, ObIORequest *&io_request, RequestHolder &req_holder)
{
  int ret = OB_SUCCESS;
  ObIOResult *io_result = nullptr;
  if (OB_FAIL(alloc_and_init_result(info, io_result))) {
    LOG_WARN("fail to alloc and init io result", K(ret));
  } else if (OB_ISNULL(io_result)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("io result is null", K(ret));
  } else if (OB_FAIL(handle.set_result(*io_result))) {
    LOG_WARN("fail to set result to handle", K(ret), KPC(io_result));
  } else if (OB_FAIL(alloc_io_request(io_request))) {
    if (OB_ALLOCATE_MEMORY_FAILED == ret) {
      LOG_WARN("alloc io request failed, retry until timeout", K(ret));
      //blocking foreground thread
      ret = OB_SUCCESS;
      if (OB_FAIL(try_alloc_req_until_timeout(ObTimeUtility::current_time() + info.timeout_us_, io_request))) {
        LOG_WARN("retry alloc io request failed", K(ret));
      }
    } else {
      LOG_WARN("alloc io request failed", K(ret), KP(io_request));
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(io_request)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("io request is null", K(ret));
    } else if (OB_FAIL(io_request->basic_init())) {
      LOG_WARN("basic init io request failed", K(ret));
    }
  } 

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(io_request->init(info, io_result))) {
    LOG_WARN("init io request failed", K(ret), KP(io_request));
  }

  if (OB_FAIL(ret)) {
    if (OB_NOT_NULL(io_request)) {
      //free io_request manually
      io_request->free();
      io_request = nullptr;
    }
  } else {
    req_holder.hold(io_request);
  }
  return ret;
}

int ObTenantIOManager::inner_aio(const ObIOInfo &info, ObIOHandle &handle)
{
  int ret = OB_SUCCESS;
  handle.reset();
  ObIORequest *req = nullptr;
  RequestHolder req_holder;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!is_working())) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("tenant not working", K(ret));
  } else if (OB_ISNULL(info.fd_.device_handle_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("device handle is null", K(ret), K(info));
  } else if (OB_FAIL(alloc_req_and_result(info, handle, req, req_holder))) {
    LOG_WARN("pre set io args failed", K(ret), K(info));
  } else if (OB_FAIL(qsched_.schedule_request(*req))) {
    LOG_WARN("schedule request failed", K(ret), KPC(req));
  }
  if (OB_FAIL(ret)) {
    // io callback should be freed by caller
    handle.clear_io_callback();
    handle.reset();
  }
  return ret;
}

int ObTenantIOManager::detect_aio(const ObIOInfo &info, ObIOHandle &handle)
{
  int ret = OB_SUCCESS;
  handle.reset();
  ObIORequest *req = nullptr;
  RequestHolder req_holder;
  ObDeviceChannel *device_channel = nullptr;
  ObTimeGuard time_guard("detect_aio_request", 100000); //100ms

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!is_working())) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("tenant not working", K(ret));
  } else if (OB_UNLIKELY(info.callback_ != nullptr || info.user_data_buf_ != nullptr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("callback and user_data_bug should be nullptr", K(ret), K(info.callback_));
  } else if (OB_FAIL(alloc_req_and_result(info, handle, req, req_holder))) {
    LOG_WARN("pre set io args failed", K(ret), K(info));
  } else if (OB_FAIL(req->prepare())) {
    LOG_WARN("prepare io request failed", K(ret), K(req));
  } else if (FALSE_IT(time_guard.click("prepare_detect_req"))) {
  } else if (OB_FAIL(OB_IO_MANAGER.get_device_channel(*req, device_channel))) {
    LOG_WARN("get device channel failed", K(ret), K(req));
  } else {
    if (OB_ISNULL(req->io_result_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("io result is null", K(ret));
    } else {
      ObThreadCondGuard guard(req->io_result_->cond_);
      if (OB_FAIL(guard.get_ret())) {
        LOG_ERROR("fail to guard master condition", K(ret));
      } else if (req->is_canceled()) {
        ret = OB_CANCELED;
      } else if (OB_FAIL(device_channel->submit(*req))) {
        if (OB_EAGAIN != ret) {
          LOG_WARN("submit io request failed", K(ret), K(*req), KPC(device_channel));
        }
      } else {
        time_guard.click("device_submit_detect");
      }
    }
  }
  if (time_guard.get_diff() > 100000) {// 100ms
    //print req
    LOG_INFO("submit_detect_request cost too much time", K(ret), K(time_guard), K(req));
  }
  if (OB_FAIL(ret)) {
    handle.reset();
  }
  return ret;
}

int ObTenantIOManager::enqueue_callback(ObIORequest &req)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!is_working())) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("tenant not working", K(ret));
  } else if (OB_FAIL(callback_mgr_.enqueue_callback(req))) {
    LOG_WARN("push io request into callback queue failed", K(ret), K(req));
  }
  return ret;
}
int ObTenantIOManager::update_basic_io_unit_config(const ObTenantIOConfig::UnitConfig &io_unit_config)
{
  int ret = OB_SUCCESS;
  bool need_adjust_callback = false;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!is_working())) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("tenant not working", K(ret));
  } else {
    // update basic io config
    if (io_config_.unit_config_.weight_ != io_unit_config.weight_
        || io_config_.unit_config_.max_iops_ != io_unit_config.max_iops_
        || io_config_.unit_config_.min_iops_ != io_unit_config.min_iops_
        || io_config_.unit_config_.max_net_bandwidth_ != io_unit_config.max_net_bandwidth_
        || io_config_.unit_config_.net_bandwidth_weight_ != io_unit_config.net_bandwidth_weight_) {
      LOG_INFO("update io unit config", K(io_config_.unit_config_), K(io_unit_config));
      io_config_.unit_config_ =io_unit_config;
      if (OB_FAIL(qsched_.update_config(io_config_))) {
        LOG_WARN("refresh tenant io config failed", K(ret), K(io_config_));
      }
    }
  }
  return ret;
}

int ObTenantIOManager::update_basic_io_param_config(const ObTenantIOConfig::ParamConfig &io_param_config)
{
  int ret = OB_SUCCESS;
  bool need_adjust_callback = false;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!is_working())) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("tenant not working", K(ret));
  } else {
    if (OB_FAIL(ret)) {
    } else if (io_config_.param_config_.memory_limit_ != io_param_config.memory_limit_) {
      LOG_INFO("update io memory limit", K(io_param_config.memory_limit_), K(io_config_.param_config_.memory_limit_));
      if (OB_FAIL(update_memory_pool(io_param_config.memory_limit_))) {
        LOG_WARN("fail to update tenant io manager memory pool", K(ret), K(io_memory_limit_), K(io_param_config.memory_limit_));
      } else {
        io_config_.param_config_.memory_limit_ = io_param_config.memory_limit_;
        need_adjust_callback = true;
      }
    }
    if (OB_FAIL(ret)) {
    } else if (io_config_.param_config_.callback_thread_count_ != io_param_config.callback_thread_count_) {
      LOG_INFO("update io callback thread count", K(io_param_config.callback_thread_count_), K(io_config_.param_config_.callback_thread_count_));
      io_config_.param_config_.callback_thread_count_ = io_param_config.callback_thread_count_;
      need_adjust_callback = true;
    }
    if (OB_SUCC(ret) && need_adjust_callback) {
      int64_t callback_thread_count = io_config_.get_callback_thread_count();
      MOD_SCOPE {
        if (OB_FAIL(callback_mgr_.update_thread_count(callback_thread_count))) {
          LOG_WARN("callback manager adjust thread failed", K(ret), K(io_param_config));
        }
      }
    }
    if (OB_FAIL(ret)) {
    } else if (io_config_.param_config_.object_storage_io_timeout_ms_ != io_param_config.object_storage_io_timeout_ms_) {
      LOG_INFO("update object storage io timeout ms", "ori_object_storage_io_timeout_ms",
               io_config_.param_config_.object_storage_io_timeout_ms_, "new_object_storage_io_timeout_ms",
               io_param_config.object_storage_io_timeout_ms_);
      io_config_.param_config_.object_storage_io_timeout_ms_ = io_param_config.object_storage_io_timeout_ms_;
    }
  }
  return ret;
}

int ObTenantIOManager::try_alloc_req_until_timeout(const int64_t timeout_ts, ObIORequest *&req)
{
  int ret = OB_SUCCESS;
  int64_t retry_alloc_count = 0;
  while (OB_SUCC(ret)) {
    ++retry_alloc_count;
    const int64_t current_ts = ObTimeUtility::current_time();
    if (current_ts > timeout_ts) {
      ret = OB_TIMEOUT;
      LOG_WARN("current time is larger than the timeout timestamp", K(ret), K(current_ts), K(timeout_ts), K(retry_alloc_count));
    } else if (OB_FAIL(alloc_io_request(req))) {
      if (OB_ALLOCATE_MEMORY_FAILED == ret) {
        const int64_t remain_time = timeout_ts - current_ts;
        const int64_t sleep_time = MIN(remain_time, 1000L);
        if (TC_REACH_TIME_INTERVAL(1000L * 1000L)) {
          LOG_INFO("execute failed, retry later", K(ret), K(remain_time), K(sleep_time), K(retry_alloc_count));
        }
        ob_usleep((useconds_t)sleep_time);
        ret = OB_SUCCESS;
      }
    } else {
      LOG_INFO("retry alloc io_request success", K(retry_alloc_count));
      break;
    }
  }
  return ret;
}

int ObTenantIOManager::try_alloc_result_until_timeout(const int64_t timeout_ts, ObIOResult *&result)
{
  int ret = OB_SUCCESS;
  int64_t retry_alloc_count = 0;
  while (OB_SUCC(ret)) {
    ++retry_alloc_count;
    const int64_t current_ts = ObTimeUtility::current_time();
    if (current_ts > timeout_ts) {
      ret = OB_TIMEOUT;
      LOG_WARN("current time is larger than the timeout timestamp", K(ret), K(current_ts), K(timeout_ts), K(retry_alloc_count));
    } else if (OB_FAIL(alloc_io_result(result))) {
      if (OB_ALLOCATE_MEMORY_FAILED == ret) {
        const int64_t remain_time = timeout_ts - current_ts;
        const int64_t sleep_time = MIN(remain_time, 1000L);
        if (TC_REACH_TIME_INTERVAL(1000L * 1000L)) {
          LOG_INFO("execute failed, retry later", K(ret), K(remain_time), K(sleep_time), K(retry_alloc_count));
        }
        ob_usleep((useconds_t)sleep_time);
        ret = OB_SUCCESS;
      }
    } else {
      LOG_INFO("retry alloc io_result success", K(retry_alloc_count));
      break;
    }
  }
  return ret;
}

int ObTenantIOManager::alloc_io_request(ObIORequest *&req)
{
  int ret = OB_SUCCESS;
  req = nullptr;
  void *buf = nullptr;
  if (OB_ISNULL(buf = io_allocator_.alloc(sizeof(ObIORequest)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate memory failed", K(ret), K(sizeof(ObIORequest)));
  } else {
    req = new (buf) ObIORequest;
    req->tenant_io_mgr_ = this;
  }
  return ret;
}

int ObTenantIOManager::alloc_io_result(ObIOResult *&result)
{
  int ret = OB_SUCCESS;
  result = nullptr;
  void *buf = nullptr;
  if (OB_ISNULL(buf = io_allocator_.alloc(sizeof(ObIOResult)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate memory failed", K(ret), K(sizeof(ObIORequest)));
  } else {
    result = new (buf) ObIOResult;
    result->tenant_io_mgr_ = this;
  }
  return ret;
}

int ObTenantIOManager::init_group_index_map(const ObTenantIOConfig &io_config)
{
  int ret = OB_SUCCESS;
  ObMemAttr attr("GROUP_INDEX_MAP");
  if (OB_FAIL(group_id_index_map_.create(7, attr, attr))) {
    LOG_WARN("create group index map failed", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < io_config.group_configs_.count(); ++i) {
      const ObTenantIOConfig::GroupConfig &config = io_config.group_configs_.at(i);
      ObIOGroupKey key(config.group_id_, config.mode_);
      if (OB_FAIL(group_id_index_map_.set_refactored(key, i, 1 /*overwrite*/))) {
        LOG_WARN("init group_index_map failed", K(ret), K(i));
      }
    }
  }
  return ret;
}

int ObTenantIOManager::get_group_index(const ObIOGroupKey &key, uint64_t &index)
{
  int ret = OB_SUCCESS;
  index = static_cast<uint64_t>(key.mode_);
  return ret;
}

const ObTenantIOConfig &ObTenantIOManager::get_io_config()
{
  return io_config_;
}

int64_t ObTenantIOManager::get_group_num()
{
  DRWLock::RDLockGuard guard(io_config_lock_);
  const uint64_t MODE_CNT = static_cast<uint64_t>(ObIOMode::MAX_MODE) + 1;
  int64_t group_num = io_config_.group_configs_.count() / MODE_CNT;
  return group_num;
}


int ObTenantIOManager::print_io_status()
{
  int ret = OB_SUCCESS;
  if (is_working() && is_inited_) {
    char io_status[1024] = { 0 };
    bool need_print_io_config = false;
    io_usage_.calculate_io_usage();
    io_sys_usage_.calculate_io_usage();
    const ObIOUsageInfoArray &info = io_usage_.get_io_usage();
    const ObIOUsageInfoArray &sys_info = io_sys_usage_.get_io_usage();
    ObIOFailedReqInfoArray &failed_req_info = io_usage_.get_failed_req_usage();
    ObIOFailedReqInfoArray &sys_failed_req_info = io_sys_usage_.get_failed_req_usage();
    const ObIOMemStat &sys_mem_stat = io_mem_stats_.get_sys_mem_stat();
    const ObIOMemStat &mem_stat = io_mem_stats_.get_mem_stat();
    const int64_t MODE_COUNT = static_cast<int64_t>(ObIOMode::MAX_MODE) + 1;
    const int64_t GROUP_MODE_CNT = static_cast<int64_t>(ObIOGroupMode::MODECNT);
    int64_t ips = 0;
    int64_t ops = 0;
    int64_t ibw = 0;
    int64_t obw = 0;
    double failed_ips = 0;
    double failed_ops = 0;
    int64_t failed_ibw = 0;
    int64_t failed_obw = 0;
    uint64_t group_config_index = 0;
    ObIOMode mode = ObIOMode::MAX_MODE;
    ObIOGroupMode group_mode = ObIOGroupMode::MODECNT;
    int tmp_ret = OB_SUCCESS;
    for (int64_t i = 0; i < info.count(); ++i) {
      if (OB_TMP_FAIL(transform_usage_index_to_group_config_index(i, group_config_index))) {
        continue;
      } else if (group_config_index >= io_config_.group_configs_.count() || info.count() != failed_req_info.count() || info.count() != mem_stat.group_mem_infos_.count()) {
        continue;
      }
      mode = static_cast<ObIOMode>(group_config_index % MODE_COUNT);
      group_mode = static_cast<ObIOGroupMode>(i % GROUP_MODE_CNT);
      ObTenantIOConfig::GroupConfig &group_config = io_config_.group_configs_.at(group_config_index);
      if (group_config.deleted_) {
        continue;
      }
      const char *group_name = i < 4 ? "OTHER_GROUPS" : group_config.group_name_;
      const char *mode_str = get_io_mode_string(group_mode);
      int64_t group_bw = 0;
      double failed_avg_size = 0;
      double failed_req_iops = 0;
      int64_t failed_req_bw = 0;
      int64_t failed_avg_prepare_delay = 0;
      int64_t failed_avg_schedule_delay = 0;
      int64_t failed_avg_submit_delay = 0;
      int64_t failed_avg_device_delay = 0;
      int64_t failed_avg_total_delay = 0;
      double iops_scale = 1.0;
      double failed_iops_scale = 1.0;
      bool is_io_ability_valid = false;  // unused
      int64_t norm_iops = 0;
      if (group_mode == ObIOGroupMode::LOCALREAD) {
        norm_iops = get_norm_iops(info.at(i).avg_byte_, info.at(i).avg_iops_, ObIOMode::READ);
      } else if (group_mode == ObIOGroupMode::LOCALWRITE) {
        norm_iops = get_norm_iops(info.at(i).avg_byte_, info.at(i).avg_iops_, ObIOMode::WRITE);
      } else {
        norm_iops = info.at(i).avg_byte_ * info.at(i).avg_iops_ / STANDARD_IOPS_SIZE;
      }
      if (OB_TMP_FAIL(failed_req_info.at(i).calc(failed_avg_size,
              failed_req_iops,
              failed_req_bw,
              failed_avg_prepare_delay,
              failed_avg_schedule_delay,
              failed_avg_submit_delay,
              failed_avg_device_delay,
              failed_avg_total_delay))) {
      } else if ((info.at(i).avg_byte_ + failed_avg_size) < std::numeric_limits<double>::epsilon()) {
      } else {
        group_bw = static_cast<int64_t>(info.at(i).avg_byte_ * info.at(i).avg_iops_);
        ObIOCalibration::get_instance().get_iops_scale(mode, failed_avg_size, failed_iops_scale, is_io_ability_valid);
        ObIOCalibration::get_instance().get_iops_scale(mode, info.at(i).avg_byte_, iops_scale, is_io_ability_valid);
        switch (group_mode) {
          case ObIOGroupMode::LOCALREAD: {
            if (iops_scale > std::numeric_limits<double>::epsilon()) {
              ips += info.at(i).avg_iops_ / iops_scale;
            }
            if (failed_iops_scale > std::numeric_limits<double>::epsilon()) {
              failed_ips += failed_req_iops / failed_iops_scale;
            }
            break;
          }
          case ObIOGroupMode::LOCALWRITE: {
            if (iops_scale > std::numeric_limits<double>::epsilon()) {
              ops += info.at(i).avg_iops_ / iops_scale;
            }
            if (failed_iops_scale > std::numeric_limits<double>::epsilon()) {
              failed_ops += failed_req_iops / failed_iops_scale;
            }
            break;
          }
          case ObIOGroupMode::REMOTEREAD: {
            failed_ibw += failed_req_bw;
            ibw += static_cast<int64_t>(info.at(i).avg_byte_ * info.at(i).avg_iops_);
            break;
          }
          case ObIOGroupMode::REMOTEWRITE: {
            failed_obw += failed_req_bw;
            obw += static_cast<int64_t>(info.at(i).avg_byte_ * info.at(i).avg_iops_);
            break;
          }
          default:
            break;
        }
        snprintf(io_status, sizeof(io_status),"group_id:%ld, group_name:%s, mode:%s, cur_req:%ld, hold_mem:%ld "
            "[FAILED]:fail_size:%ld, fail_iops:%ld, fail_bw:%ld, [delay/us]:prepare:%ld, schedule:%ld, submit:%ld, rt:%ld, total:%ld, "
            "[SUCC]:size:%ld, iops:%ld, norm_iops:%ld, bw:%ld, [delay/us]:prepare:%ld, schedule:%ld, submit:%ld, rt:%ld, total:%ld",
            group_config.group_id_,
            group_name,
            mode_str,
            mem_stat.group_mem_infos_.at(i).total_cnt_,
            mem_stat.group_mem_infos_.at(i).total_size_,
            static_cast<int64_t>(failed_avg_size),
            static_cast<int64_t>(failed_req_iops + 0.5),
            static_cast<int64_t>(failed_req_bw),
            failed_avg_prepare_delay,
            failed_avg_schedule_delay,
            failed_avg_submit_delay,
            failed_avg_device_delay,
            failed_avg_total_delay,
            static_cast<int64_t>(info.at(i).avg_byte_),
            static_cast<int64_t>(info.at(i).avg_iops_ + 0.5),
            norm_iops,
            static_cast<int64_t>(group_bw),
            info.at(i).avg_prepare_delay_us_,
            info.at(i).avg_schedule_delay_us_,
            info.at(i).avg_submit_delay_us_,
            info.at(i).avg_device_delay_us_,
            info.at(i).avg_total_delay_us_
            );
        LOG_INFO("[IO STATUS GROUP]", KCSTRING(io_status));
        need_print_io_config = true;
      }
    }
    // MOCK SYS GROUPS
    for (int64_t i = 0; i < sys_info.count(); ++i) {
      if (OB_TMP_FAIL(transform_usage_index_to_group_config_index(i, group_config_index))) {
        continue;
      } else if (sys_info.count() != sys_failed_req_info.count()) {
        continue;
      }
      mode = static_cast<ObIOMode>(group_config_index % MODE_COUNT);
      group_mode = static_cast<ObIOGroupMode>(i % GROUP_MODE_CNT);
      ObIOModule module = static_cast<ObIOModule>(SYS_MODULE_START_ID + i / GROUP_MODE_CNT);
      const char *mode_str = get_io_mode_string(group_mode);
      int64_t group_bw = 0;
      double failed_avg_size = 0;
      double failed_req_iops = 0;
      int64_t failed_req_bw = 0;
      double iops_scale = 1.0;
      bool is_io_ability_valid = false;  // unused
      double failed_iops_scale = 1.0;
      int64_t failed_avg_prepare_delay = 0;
      int64_t failed_avg_schedule_delay = 0;
      int64_t failed_avg_submit_delay = 0;
      int64_t failed_avg_device_delay = 0;
      int64_t failed_avg_total_delay = 0;
      int64_t norm_iops = 0;
      int64_t norm_failed_iops = 0;
      if (OB_TMP_FAIL(sys_failed_req_info.at(i).calc(failed_avg_size,
              failed_req_iops,
              failed_req_bw,
              failed_avg_prepare_delay,
              failed_avg_schedule_delay,
              failed_avg_submit_delay,
              failed_avg_device_delay,
              failed_avg_total_delay))) {
      } else if ((sys_info.at(i).avg_byte_ + failed_avg_size) < std::numeric_limits<double>::epsilon()) {
      } else {
        switch (group_mode) {
          case ObIOGroupMode::LOCALREAD: {
            norm_iops = get_norm_iops(sys_info.at(i).avg_byte_, sys_info.at(i).avg_iops_, ObIOMode::READ);
            norm_failed_iops = get_norm_iops(failed_avg_size, failed_req_iops, ObIOMode::READ);
            ips += norm_iops;
            failed_ips += norm_failed_iops;
            break;
          }
          case ObIOGroupMode::LOCALWRITE: {
            norm_iops = get_norm_iops(sys_info.at(i).avg_byte_, sys_info.at(i).avg_iops_, ObIOMode::WRITE);
            norm_failed_iops = get_norm_iops(failed_avg_size, failed_req_iops, ObIOMode::WRITE);
            ops += norm_iops;
            failed_ops += norm_failed_iops;
            break;
          }
          case ObIOGroupMode::REMOTEREAD: {
            ibw += static_cast<int64_t>(sys_info.at(i).avg_byte_ * sys_info.at(i).avg_iops_);
            failed_ibw += failed_req_bw;
            break;
          }
          case ObIOGroupMode::REMOTEWRITE: {
            obw += static_cast<int64_t>(sys_info.at(i).avg_byte_ * sys_info.at(i).avg_iops_);
            failed_obw += failed_req_bw;
            break;
          }
          default:
            break;
        }
        group_bw = static_cast<int64_t>(sys_info.at(i).avg_byte_ * sys_info.at(i).avg_iops_);
        snprintf(io_status, sizeof(io_status),
                "sys_group_name:%s, mode:%s, cur_req:%ld, hold_mem:%ld "
                "[FAILED]: fail_size:%ld, fail_iops:%ld, fail_bw:%ld, [delay/us]:prepare:%ld, schedule:%ld, submit:%ld, rt:%ld, total:%ld, "
                "[SUCC]: size:%ld, iops:%ld, norm_iops:%ld, bw:%ld, [delay/us]:prepare:%ld, schedule:%ld, submit:%ld, rt:%ld, total:%ld",
                 get_io_sys_group_name(module),
                 mode_str,
                 sys_mem_stat.group_mem_infos_.at(i).total_cnt_,
                 sys_mem_stat.group_mem_infos_.at(i).total_size_,
                 static_cast<int64_t>(failed_avg_size),
                 static_cast<int64_t>(failed_req_iops + 0.5),
                 static_cast<int64_t>(failed_req_bw),
                 failed_avg_prepare_delay,
                 failed_avg_schedule_delay,
                 failed_avg_submit_delay,
                 failed_avg_device_delay,
                 failed_avg_total_delay,
                 static_cast<int64_t>(sys_info.at(i).avg_byte_),
                 static_cast<int64_t>(sys_info.at(i).avg_iops_ + 0.5),
                 norm_iops,
                 static_cast<int64_t>(group_bw),
                 sys_info.at(i).avg_prepare_delay_us_,
                 sys_info.at(i).avg_schedule_delay_us_,
                 sys_info.at(i).avg_submit_delay_us_,
                 sys_info.at(i).avg_device_delay_us_,
                 sys_info.at(i).avg_total_delay_us_
                 );
        LOG_INFO("[IO STATUS GROUP SYS]", KCSTRING(io_status));
        need_print_io_config = true;
      }
    }
    if (need_print_io_config) {
      int64_t iops = ips + ops;
      double failed_iops = failed_ips + failed_ops;
      LOG_INFO("[IO STATUS TENANT]", K_(ref_cnt), K_(io_config),
          "hold_mem", io_allocator_.get_allocated_size(),
          "[FAILED]: "
          "fail_ips", lround(failed_ips),
          "fail_ops", lround(failed_ops),
          "fail_iops", lround(failed_iops),
          "fail_ibw", failed_ibw,
          "fail_obw", failed_obw,
          "[SUCC]: "
          "ips", ips,
          "ops", ops,
          "iops", iops,
          "ibw", ibw,
          "obw", obw,
          "iops_limit", 0,
          "ibw_limit", 0,
          "obw_limit", 0);
    }

    // print callback status
    {
      (void)callback_mgr_.to_string(io_status, sizeof(io_status));
      LOG_INFO("[IO STATUS CALLBACK]", KCSTRING(io_status));
    }
  }
  return ret;
}

void ObTenantIOManager::inc_ref()
{
  ATOMIC_INC(&ref_cnt_);
}

void ObTenantIOManager::dec_ref()
{
  int ret = OB_SUCCESS;
  int64_t tmp_ref = ATOMIC_SAF(&ref_cnt_, 1);
  if (tmp_ref < 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("bug: ref_cnt < 0", K(ret), K(tmp_ref));
    abort();
  }
}

int ObTenantIOManager::get_throttled_time(uint64_t group_id, int64_t &throttled_time)
{
  int ret = OB_SUCCESS;
  UNUSED(group_id);
  throttled_time = 0;
  return ret;
}
