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

#define USING_LOG_PREFIX RS_LB

#include "ob_balance_info.h"
#include "ob_resource_weight_parser.h"

using namespace oceanbase::common;
using namespace oceanbase::common::hash;
using namespace oceanbase::rootserver;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;

////////////////
ObStatisticsCalculator::ObStatisticsCalculator()
    :sum_(0)
{}



double ObStatisticsCalculator::get_avg()
{
  double avg = 0;
  if (values_.count() > 0) {
    avg = sum_ / static_cast<double>(values_.count());
  }
  return avg;
}


int ZoneUnit::assign(const ZoneUnit &other)
{
  int ret = OB_SUCCESS;
  zone_ = other.zone_;
  active_unit_cnt_ = other.active_unit_cnt_;

  load_imbalance_ = other.load_imbalance_;
  cpu_imbalance_ = other.cpu_imbalance_;
  disk_imbalance_ = other.disk_imbalance_;
  iops_imbalance_ = other.iops_imbalance_;
  memory_imbalance_ = other.memory_imbalance_;
  load_avg_ = other.load_avg_;
  cpu_avg_ = other.cpu_avg_;
  disk_avg_ = other.disk_avg_;
  iops_avg_ = other.iops_avg_;
  memory_avg_ = other.memory_avg_;

  tg_pg_cnt_ = other.tg_pg_cnt_;
  if (OB_FAIL(copy_assign(all_unit_, other.all_unit_))) {
    LOG_WARN("failed to assign all_unit_", K(ret));
  }
  return ret;
}

bool ServerStat::can_migrate_in() const
{
  return !blocked_ && active_ && online_;;
}


int UnitStat::assign(const UnitStat &other)
{
  int ret = OB_SUCCESS;
  server_ = other.server_;
  in_pool_ = other.in_pool_;
  load_factor_ = other.load_factor_;
  capacity_ = other.capacity_;
  load_ = other.load_;
  tg_pg_cnt_ = other.tg_pg_cnt_;
  outside_replica_cnt_ = other.outside_replica_cnt_;
  inside_replica_cnt_ = other.inside_replica_cnt_;
  if (OB_FAIL(copy_assign(info_, other.info_))) {
    LOG_WARN("failed to assign info_", K(ret));
  }
  return ret;
}


double UnitStat::get_load_if(ObResourceWeight &weights,
                                      const LoadFactor &load_factor, const bool plus) const
{
  LoadFactor new_factor = load_factor_;
  if (plus) {
    new_factor += load_factor;
  } else {
    new_factor -= load_factor;
  }
  return weights.cpu_weight_ * (new_factor.get_cpu_usage()/get_cpu_limit())
         + weights.memory_weight_ * (new_factor.get_memory_usage()/get_memory_limit())
         + weights.disk_weight_ * (new_factor.get_disk_usage()/get_disk_limit())
         + weights.iops_weight_ * (new_factor.get_iops_usage()/get_iops_limit());
}




