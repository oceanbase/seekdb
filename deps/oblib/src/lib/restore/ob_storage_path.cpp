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

#include "lib/utility/ob_print_utils.h"
#include "ob_storage_path.h"

namespace oceanbase
{

namespace common
{

ObStoragePath::ObStoragePath()
  :cur_pos_(0)
{
  MEMSET(path_, 0, sizeof(path_));
}

ObStoragePath::~ObStoragePath()
{
  cur_pos_ = 0;
  MEMSET(path_, 0, sizeof(path_));
}

int ObStoragePath::trim_right_delim()
{
  int ret = OB_SUCCESS;
  if (cur_pos_ <= 0) {
    ret = OB_NOT_INIT;
    OB_LOG(WARN, "please init first", K(ret));
  } else {
    while ('/' == path_[cur_pos_] && cur_pos_ > 0) {
      --cur_pos_;
    }
  }
  return ret;
}

int ObStoragePath::trim_left_delim(const common::ObString &path, int64_t &delim_pos)
{
  int ret = OB_SUCCESS;
  delim_pos = 0;
  if (path.empty()) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "argument is invalid", K(ret), K(path));
  } else {
    while ('/' == *(path.ptr() + delim_pos) && delim_pos < path.length()) {
      ++delim_pos;
    }
  }
  return ret;
}

int ObStoragePath::init(const common::ObString &uri)
{
  int ret = OB_SUCCESS;

  if (uri.empty()) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "argument is invalid", K(ret), K(uri));
  } else if (0 != cur_pos_) {
    ret = OB_INIT_TWICE;
    OB_LOG(WARN, "already inited", K(ret));
  } else if (OB_FAIL(databuff_printf(path_, common::OB_MAX_URI_LENGTH, cur_pos_, "%.*s",
      uri.length(), uri.ptr()))) {
    OB_LOG(WARN, "fail to save header uri", K(ret), K(uri));
  } else if (OB_FAIL(trim_right_delim())) {
    OB_LOG(WARN, "fail to trim right delim", K(ret));
  }
  OB_LOG(DEBUG, "", KCSTRING(path_), K(cur_pos_));
  return ret;
}


int ObStoragePath::join(const common::ObString &path)
{
  int ret = OB_SUCCESS;
  int64_t left_delim_pos = 0;

  if (cur_pos_ <= 0) {
    ret = OB_NOT_INIT;
    OB_LOG(WARN, "please init first", K(ret));
  } else if (path.empty()) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "argument is invalid", K(ret), K(path));
  } else if (OB_FAIL(trim_left_delim(path, left_delim_pos))) {
    OB_LOG(WARN, "fail to trim left delim", K(ret), K(path));
  } else if (OB_FAIL(databuff_printf(path_, common::OB_MAX_CONFIG_URL_LENGTH, cur_pos_, "/%.*s",
      static_cast<int>(path.length() - left_delim_pos), path.ptr() + left_delim_pos))) {
    OB_LOG(WARN, "fail to join path", K(ret), K(path));
  } else if (OB_FAIL(trim_right_delim())) {
    OB_LOG(WARN, "fail to trim right delim", K(ret));
  }
  OB_LOG(DEBUG, "", KCSTRING(path_), K(cur_pos_));
  return ret;
}



int ObStoragePath::join_simple_object_definition(uint64_t object_id)
{
  int ret = OB_SUCCESS;
  if (cur_pos_ < 0) {
    ret = OB_NOT_INIT;
    OB_LOG(WARN, "please init first", K(ret));
  } else if (object_id <= 0) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "argument is invalid", K(ret), K(object_id));
  } else if (OB_FAIL(databuff_printf(path_, common::OB_MAX_URI_LENGTH, cur_pos_,
                                     "/%ld_definition", object_id))) {
    OB_LOG(WARN, "fail to join simple object definition", K(ret), KCSTRING(path_), K(object_id));
  }
  return ret;
}






int64_t ObStoragePath::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  databuff_printf(buf, buf_len, pos, "%.*s",static_cast<int>(cur_pos_), path_);
  return pos;
}



















}//common
}//oceanbase
