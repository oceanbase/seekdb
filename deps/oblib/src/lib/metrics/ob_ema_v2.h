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

#ifndef _OB_EMA_V2_H
#define _OB_EMA_V2_H 1
#include "lib/utility/ob_macro_utils.h"
#include "lib/atomic/ob_atomic.h"
namespace oceanbase
{
namespace common
{
// See https://en.wikipedia.org/wiki/Moving_average#Exponential_moving_average
class ObExponentialMovingAverageV2
{
public:
  ObExponentialMovingAverageV2(const double alpha)
      :inited_(false),
       ema_(0.0),
       alpha_(alpha)
  {}
  ~ObExponentialMovingAverageV2() {}

  void update(uint64_t value)
  {
    const double instant_rate = static_cast<double>(value);
    if (ATOMIC_LOAD(&inited_)) {
      // EMA_new = EMA_old + alpha * (value - EMA_old)
      const double old_ema = ATOMIC_LOAD64(&ema_);
      double new_ema = old_ema + (alpha_ * (instant_rate - old_ema));
      ATOMIC_SET(reinterpret_cast<int64_t*>(&ema_), *reinterpret_cast<int64_t*>(&new_ema));
    } else {
      inited_ = true;
      ema_ = instant_rate;
    }
  }
  double get_value() const
  {
    return ATOMIC_LOAD64(&ema_);
  }

private:
  bool inited_;  // atomic
  double ema_;  // atomic
  const double alpha_; // const
  DISALLOW_COPY_AND_ASSIGN(ObExponentialMovingAverageV2);
};

typedef ObExponentialMovingAverageV2 ObEMA;

} // end namespace common
} // end namespace oceanbase

#endif /* _OB_EMA_V2_H */
