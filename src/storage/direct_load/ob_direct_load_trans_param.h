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

#pragma once

#include "storage/tx/ob_trans_define.h"

namespace oceanbase
{
namespace storage
{

struct ObDirectLoadTransParam
{
public:
  ObDirectLoadTransParam() : tx_desc_(nullptr) {}
  ~ObDirectLoadTransParam() {}
  void reset()
  {
    tx_desc_ = nullptr;
    tx_id_.reset();
    tx_seq_.reset();
  }
  bool is_valid() const { return nullptr != tx_desc_ && tx_id_.is_valid() && tx_seq_.is_valid(); }
  TO_STRING_KV(KPC_(tx_desc), K_(tx_id), K_(tx_seq));

public:
  transaction::ObTxDesc *tx_desc_;
  transaction::ObTransID tx_id_;
  transaction::ObTxSEQ tx_seq_;
};

} // namespace storage
} // namespace oceanbase
