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
#define USING_LOG_PREFIX SQL_ENG
#include "ob_mul_mode_reader.h"

namespace oceanbase {
namespace common {




void ObMulModeReader::init()
{
  if (OB_NOT_NULL(cur_)) {
    bool is_simple_scan = false;
    bool is_ordered_scan = false;
    bool is_attr_scan = false;

    if (!(flags_ & SEEK_FLAG)) {
      is_simple_scan = true;
    } else if (seek_info_.type_ == ALL_ARR_TYPE || seek_info_.type_ == ALL_KEY_TYPE) {
      is_simple_scan = true;
    } else if (seek_info_.type_ == KEY_TYPE) {
      is_ordered_scan = true;
    } else if (seek_info_.type_ == ATTR_KEY) {
      is_attr_scan = true;
    }

    if (is_simple_scan) {
      new (&tree_iter_) ObXmlNode::iterator(static_cast<ObXmlNode*>(cur_)->begin());
    } else if (is_ordered_scan) {
      IterRange range;
      ObXmlNode* node = static_cast<ObXmlNode*>(cur_);
      node->ObLibContainerNode::get_children(seek_info_.key_, range);
      new (&tree_iter_) ObXmlNode::iterator(node->sorted_begin());
      tree_iter_.set_range(range.first - tree_iter_, range.second - tree_iter_ + 1);
    } else if (is_attr_scan) {
      ObXmlNode* handle = nullptr;
      if (!(cur_->type() == M_ELEMENT || cur_->type() == M_DOCUMENT)) {
        new (&tree_iter_) ObXmlNode::iterator(static_cast<ObXmlNode*>(cur_)->sorted_begin());
        tree_iter_.set_range(0, 0);
      } else if (OB_ISNULL(handle = static_cast<ObXmlNode*>(cur_->get_attribute_handle()))) {
        new (&tree_iter_) ObXmlNode::iterator(static_cast<ObXmlNode*>(cur_)->sorted_begin());
        tree_iter_.set_range(0, 0);
      } else {
        new (&tree_iter_) ObXmlNode::iterator(handle->begin());
      }
    }
  }
}



void ObMulModeReader::alter_filter(ObMulModeFilter* filter)
{
  seek_info_.filter_ = filter;
}


int ObMulModeReader::attr_next(ObIMulModeBase*& node, ObMulModeNodeType filter_type)
{
  INIT_SUCC(ret);

  if (OB_ISNULL(cur_) || cur_->data_type() != OB_XML_TYPE || seek_info_.type_ != ATTR_KEY) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cur_ is null or data is not xml type not supported yet.", K(ret), KP(cur_), K(seek_info_.type_));
  } else {
    bool is_found = false;

    for (; OB_SUCC(ret) && !is_found; ) {
      if (tree_iter_.end()) {
        ret = OB_ITER_END;
      } else {
        node = *tree_iter_;
        ++tree_iter_;
      }

      is_found = false;
      if (OB_FAIL(ret)) {
      } else if (OB_ISNULL(seek_info_.filter_)) {
        is_found = true;
      } else if (OB_FAIL((*seek_info_.filter_)(node, is_found))) {
        LOG_WARN("failed to filter node.", K(ret));
      }
    }
  }

  return ret;
}

int ObMulModeReader::scan_next(ObIMulModeBase*& node)
{
  INIT_SUCC(ret);
  bool is_found = false;

  while (OB_SUCC(ret) && !is_found) {
    if (tree_iter_.end()) {
      ret = OB_ITER_END;
    } else {
      node = *tree_iter_;
      ++tree_iter_;
    }

    if (OB_SUCC(ret)) {
      if (OB_ISNULL(seek_info_.filter_)) {
        is_found = true;
      } else if (OB_FAIL((*seek_info_.filter_)(node, is_found))) {
        LOG_WARN("failed to filter node.", K(ret));
      }
    }
  }

  return ret;
}

int ObMulModeReader::next(ObIMulModeBase*& node)
{
  INIT_SUCC(ret);

  if (OB_ISNULL(cur_) || cur_->data_type() != OB_XML_TYPE) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cur_ is null or data is not xml type not supported yet.", K(ret), KP(cur_));
  } else {
    if (!(flags_ & SEEK_FLAG)) {
      if (OB_FAIL(scan_next(node))) {
        LOG_WARN("fail to filter next node.", K(ret));
      }
    } else if (seek_info_.type_ == KEY_TYPE) {
      if (get_mul_mode_tc(cur_->type()) != MulModeContainer) {
        ret = OB_ITER_END;
      } else if (OB_FAIL(scan_next(node))) {
        LOG_WARN("fail to get key match children xnode.", K(ret));
      }
    } else if (seek_info_.type_ == INDEX_TYPE) {
      node = cur_->at(seek_info_.index_);
      if (OB_ISNULL(node)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("fail to get node.", K(ret), K(seek_info_.index_));
      }
    } else if (seek_info_.type_ == ALL_ARR_TYPE || seek_info_.type_ == ALL_KEY_TYPE) {
      if (get_mul_mode_tc(cur_->type()) != MulModeContainer) {
        ret = OB_ITER_END;
      } else if (OB_FAIL(scan_next(node))) {
        LOG_WARN("fail to filter next node.", K(ret));
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to all children xnode.", K(ret), K(seek_info_.type_), K(cur_->data_type()));
    }
  }

  return ret;
}


} // namespace common
} // namespace oceanbase
