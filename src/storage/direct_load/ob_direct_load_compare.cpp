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
#define USING_LOG_PREFIX STORAGE

#include "storage/direct_load/ob_direct_load_compare.h"
#include "storage/direct_load/ob_direct_load_external_multi_partition_row.h"

namespace oceanbase
{
namespace storage
{
using namespace common;
using namespace blocksstable;

/**
 * ObDirectLoadDatumRowkeyCompare
 */

int ObDirectLoadDatumRowkeyCompare::init(const ObStorageDatumUtils &datum_utils)
{
  int ret = OB_SUCCESS;
  datum_utils_ = &datum_utils;
  return ret;
}

int ObDirectLoadDatumRowkeyCompare::compare(const ObDatumRowkey *lhs, const ObDatumRowkey *rhs,
                                            int &cmp_ret)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(datum_utils_) || OB_ISNULL(lhs) || OB_ISNULL(rhs) ||
      OB_UNLIKELY(!lhs->is_valid() || !rhs->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), KP(datum_utils_), KP(lhs), KP(rhs));
  } else {
    if (OB_FAIL(lhs->compare(*rhs, *datum_utils_, cmp_ret))) {
      LOG_WARN("fail to compare rowkey", KR(ret), KP(lhs), K(rhs), K(datum_utils_));
    }
  }
  return ret;
}


bool ObDirectLoadDatumRowkeyCompare::operator()(const ObDatumRowkey &lhs, const ObDatumRowkey &rhs)
{
  int ret = OB_SUCCESS;
  int cmp_ret = 0;
  if (OB_ISNULL(datum_utils_)|| OB_UNLIKELY(!lhs.is_valid() || !rhs.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), KP(datum_utils_), K(lhs), K(rhs));
  } else {
    if (OB_FAIL(lhs.compare(rhs, *datum_utils_, cmp_ret))) {
      LOG_WARN("fail to compare rowkey", KR(ret), K(lhs), K(rhs), K(datum_utils_));
    }
  }
  if (OB_FAIL(ret)) {
    result_code_ = ret;
  }
  return cmp_ret < 0;
}

bool ObDirectLoadDatumRowkeyCompare::operator()(const ObDatumRowkey *lhs, const ObDatumRowkey *rhs)
{
  int ret = OB_SUCCESS;
  int cmp_ret = 0;
  if (OB_ISNULL(datum_utils_) || OB_ISNULL(lhs) || OB_ISNULL(rhs) ||
      OB_UNLIKELY(!lhs->is_valid() || !rhs->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), KP(datum_utils_), KP(lhs), KP(rhs));
  } else {
    if (OB_FAIL(lhs->compare(*rhs, *datum_utils_, cmp_ret))) {
      LOG_WARN("fail to compare rowkey", KR(ret), KP(lhs), K(rhs), K(datum_utils_));
    }
  }
  if (OB_FAIL(ret)) {
    result_code_ = ret;
  }
  return cmp_ret < 0;
}

/**
 * ObDirectLoadSingleDatumCompare
 */




/**
 * ObDirectLoadDatumRowCompare
 */




/**
 * ObDirectLoadDatumArrayCompare
 */

int ObDirectLoadDatumArrayCompare::init(const ObStorageDatumUtils &datum_utils)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObDirectLoadDatumArrayCompare init twice", KR(ret), KP(this));
  } else {
    if (OB_FAIL(rowkey_compare_.init(datum_utils))) {
      LOG_WARN("fail to init rowkey compare", KR(ret));
    } else {
      is_inited_ = true;
    }
  }
  return ret;
}

int ObDirectLoadDatumArrayCompare::compare(const ObDirectLoadDatumArray *lhs,
                                           const ObDirectLoadDatumArray *rhs, int &cmp_ret)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDirectLoadDatumArrayCompare not init", KR(ret), KP(this));
  } else if (OB_UNLIKELY(nullptr == lhs || nullptr == rhs || !lhs->is_valid() || !rhs->is_valid() ||
                         lhs->count_ != rhs->count_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), KP(lhs), KP(rhs));
  } else if (lhs->count_ > 0) {
    if (OB_FAIL(lhs_rowkey_.assign(lhs->datums_, lhs->count_))) {
      LOG_WARN("fail to assign rowkey", KR(ret));
    } else if (OB_FAIL(rhs_rowkey_.assign(rhs->datums_, rhs->count_))) {
      LOG_WARN("fail to assign rowkey", KR(ret));
    } else if (OB_FAIL(rowkey_compare_.compare(&lhs_rowkey_, &rhs_rowkey_, cmp_ret))) {
      LOG_WARN("fail to compare rowkey", KR(ret), KP(lhs), K(rhs), K(cmp_ret));
    }
  }
  return ret;
}


int ObDirectLoadDatumArrayCompare::compare(const ObDirectLoadConstDatumArray *lhs,
                                           const ObDirectLoadConstDatumArray *rhs, int &cmp_ret)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDirectLoadDatumArrayCompare not init", KR(ret), KP(this));
  } else if (OB_UNLIKELY(nullptr == lhs || nullptr == rhs || !lhs->is_valid() || !rhs->is_valid() ||
                         lhs->count_ != rhs->count_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), KP(lhs), KP(rhs));
  } else if (lhs->count_ > 0) {
    if (OB_FAIL(lhs_rowkey_.assign(lhs->datums_, lhs->count_))) {
      LOG_WARN("fail to assign rowkey", KR(ret));
    } else if (OB_FAIL(rhs_rowkey_.assign(rhs->datums_, rhs->count_))) {
      LOG_WARN("fail to assign rowkey", KR(ret));
    } else if (OB_FAIL(rowkey_compare_.compare(&lhs_rowkey_, &rhs_rowkey_, cmp_ret))) {
      LOG_WARN("fail to compare rowkey", KR(ret), KP(lhs), K(rhs), K(cmp_ret));
    }
  }
  return ret;
}


/**
 * ObDirectLoadExternalRowCompare
 */



int ObDirectLoadExternalRowCompare::compare(const ObDirectLoadExternalRow *lhs,
                                            const ObDirectLoadExternalRow *rhs, int &cmp_ret)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDirectLoadDatumRowCompare not init", KR(ret), KP(this));
  } else if (OB_UNLIKELY(nullptr == lhs || nullptr == rhs || !lhs->is_valid() ||
                         !rhs->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), KP(lhs), KP(rhs));
  } else if (OB_FAIL(datum_array_compare_.compare(&lhs->rowkey_datum_array_,
                                                  &rhs->rowkey_datum_array_, cmp_ret))) {
    LOG_WARN("fail to compare rowkey", KR(ret), KP(lhs), K(rhs), K(cmp_ret));
  } else {
    if (cmp_ret == 0 && !ignore_seq_no_) {
      if (lhs->seq_no_ == rhs->seq_no_) {
        cmp_ret = 0;
      } else if (lhs->seq_no_ > rhs->seq_no_) {
        if (dup_action_ == sql::ObLoadDupActionType::LOAD_REPLACE) {
          cmp_ret = -1;
        } else {
          cmp_ret = 1;
        }
      } else {
        if (dup_action_ == sql::ObLoadDupActionType::LOAD_REPLACE) {
          cmp_ret = 1;
        } else {
          cmp_ret = -1;
        }
      }
    }
  }
  return ret;
}

/**
 * ObDirectLoadExternalMultiPartitionRowCompare
 */

int ObDirectLoadExternalMultiPartitionRowCompare::init(const ObStorageDatumUtils &datum_utils,
                                                       sql::ObLoadDupActionType dup_action,
                                                        bool ignore_seq_no)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObDirectLoadExternalMultiPartitionRowCompare init twice", KR(ret), KP(this));
  } else {
    if (OB_FAIL(datum_array_compare_.init(datum_utils))) {
      LOG_WARN("fail to init datum array compare", KR(ret));
    } else {
      dup_action_ = dup_action;
      ignore_seq_no_ = ignore_seq_no;
      is_inited_ = true;
    }
  }
  return ret;
}



bool ObDirectLoadExternalMultiPartitionRowCompare::operator()(
  const ObDirectLoadConstExternalMultiPartitionRow *lhs,
  const ObDirectLoadConstExternalMultiPartitionRow *rhs)
{
  int ret = OB_SUCCESS;
  int cmp_ret = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDirectLoadExternalMultiPartitionRowCompare not init", KR(ret), KP(this));
  } else if (OB_UNLIKELY(nullptr == lhs || nullptr == rhs || !lhs->is_valid() ||
                         !rhs->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), KP(lhs), KP(rhs));
  } else if (OB_FAIL(compare(lhs, rhs, cmp_ret))) {
    LOG_WARN("Fail to compare datum array", KR(ret), KP(lhs), KP(rhs));
  }
  if (OB_FAIL(ret)) {
    result_code_ = ret;
  }
  return cmp_ret < 0;
}

int ObDirectLoadExternalMultiPartitionRowCompare::compare(
  const ObDirectLoadExternalMultiPartitionRow *lhs,
  const ObDirectLoadExternalMultiPartitionRow *rhs, int &cmp_ret)
{
  int ret = OB_SUCCESS;
  bool bret = false;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDirectLoadExternalMultiPartitionRowCompare not init", KR(ret), KP(this));
  } else if (OB_UNLIKELY(nullptr == lhs || nullptr == rhs || !lhs->is_valid() ||
                         !rhs->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), KP(lhs), KP(rhs));
  } else {
    if (lhs->tablet_id_ != rhs->tablet_id_) {
      cmp_ret = lhs->tablet_id_ < rhs->tablet_id_ ? -1 : 1;
    } else if (OB_FAIL(datum_array_compare_.compare(&lhs->external_row_.rowkey_datum_array_,
                                                    &rhs->external_row_.rowkey_datum_array_,
                                                    cmp_ret))) {
      LOG_WARN("fail to compare rowkey", KR(ret), KP(lhs), K(rhs), K(cmp_ret));
    } else if (cmp_ret == 0 && !ignore_seq_no_) {
      if (lhs->external_row_.seq_no_ == rhs->external_row_.seq_no_) {
        cmp_ret = 0;
      } else if (lhs->external_row_.seq_no_ > rhs->external_row_.seq_no_) {
        if (dup_action_ == sql::ObLoadDupActionType::LOAD_REPLACE) {
          cmp_ret = -1;
        } else {
          cmp_ret = 1;
        }
      } else {
        if (dup_action_ == sql::ObLoadDupActionType::LOAD_REPLACE) {
          cmp_ret = 1;
        } else {
          cmp_ret = -1;
        }
      }
    }
  }
  return ret;
}

int ObDirectLoadExternalMultiPartitionRowCompare::compare(
  const ObDirectLoadConstExternalMultiPartitionRow *lhs,
  const ObDirectLoadConstExternalMultiPartitionRow *rhs, int &cmp_ret)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDirectLoadExternalMultiPartitionRowCompare not init", KR(ret), KP(this));
  } else if (OB_UNLIKELY(nullptr == lhs || nullptr == rhs || !lhs->is_valid() ||
                         !rhs->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), KP(lhs), KP(rhs));
  } else {
    if (lhs->tablet_id_ != rhs->tablet_id_) {
      cmp_ret = lhs->tablet_id_ < rhs->tablet_id_ ? -1 : 1;
    } else if (OB_FAIL(datum_array_compare_.compare(&lhs->rowkey_datum_array_,
                                                    &rhs->rowkey_datum_array_, cmp_ret))) {
      LOG_WARN("fail to compare rowkey", KR(ret), KP(lhs), K(rhs), K(cmp_ret));
    } else if (cmp_ret == 0 && !ignore_seq_no_) {
      if (lhs->seq_no_ == rhs->seq_no_) {
        cmp_ret = 0;
      } else if (lhs->seq_no_ > rhs->seq_no_) {
        if (dup_action_ == sql::ObLoadDupActionType::LOAD_REPLACE) {
          cmp_ret = -1;
        } else {
          cmp_ret = 1;
        }
      } else {
        if (dup_action_ == sql::ObLoadDupActionType::LOAD_REPLACE) {
          cmp_ret = 1;
        } else {
          cmp_ret = -1;
        }
      }
    }
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
