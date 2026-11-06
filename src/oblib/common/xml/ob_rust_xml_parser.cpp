/*
 * Copyright (c) 2026 OceanBase.
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
#define USING_LOG_PREFIX LIB

#include "common/xml/ob_xml_parser.h"
#include "common/xml/ob_xml_util.h"
#include "rust/xml-parser/include/callbacks.h"
#include "rust/xml_parser.h"

namespace oceanbase {
namespace common {

namespace {
ObXmlParser* parser_from_context(uint8_t* context)
{
  return reinterpret_cast<ObXmlParser*>(context);
}

int borrowed_string(const uint8_t* value, size_t value_len, ObString& output)
{
  int ret = OB_SUCCESS;
  if (value_len > INT32_MAX || (value_len > 0 && OB_ISNULL(value))) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    output.assign_ptr(reinterpret_cast<const char*>(value), static_cast<int32_t>(value_len));
  }
  return ret;
}

int map_parse_status(int status)
{
  int ret = status;
  if (status == 1) {
    ret = OB_ERR_PARSER_SYNTAX;
  } else if (status == 2) {
    ret = OB_ERR_UNEXPECTED;
  } else if (status == 3) {
    ret = OB_INVALID_ARGUMENT;
  }
  return ret;
}
} // namespace

int xml_on_start_document(uint8_t* context)
{
  ObXmlParser* parser = parser_from_context(context);
  return OB_ISNULL(parser) ? OB_INVALID_ARGUMENT : parser->start_document();
}

int xml_on_end_document(uint8_t* context)
{
  ObXmlParser* parser = parser_from_context(context);
  return OB_ISNULL(parser) ? OB_INVALID_ARGUMENT : parser->end_document();
}

int xml_on_declaration(uint8_t* context, const uint8_t* value, size_t value_len)
{
  ObXmlParser* parser = parser_from_context(context);
  if (OB_ISNULL(parser)) {
    return OB_INVALID_ARGUMENT;
  }
  ObString declaration;
  if (OB_SUCCESS != borrowed_string(value, value_len, declaration)) {
    return OB_INVALID_ARGUMENT;
  }
  return parser->set_xml_decl(declaration);
}

int xml_on_start_element(uint8_t* context,
                         const uint8_t* name,
                         size_t name_len,
                         const XmlAttributeView* attributes,
                         size_t attribute_count,
                         bool empty)
{
  ObXmlParser* parser = parser_from_context(context);
  if (OB_ISNULL(parser) || (name_len > 0 && OB_ISNULL(name))) {
    return OB_INVALID_ARGUMENT;
  }
  ObString name_str;
  if (OB_SUCCESS != borrowed_string(name, name_len, name_str)) {
    return OB_INVALID_ARGUMENT;
  }
  return parser->start_element(name_str, attributes, attribute_count, empty);
}

int xml_on_end_element(uint8_t* context, bool empty)
{
  ObXmlParser* parser = parser_from_context(context);
  return OB_ISNULL(parser) ? OB_INVALID_ARGUMENT : parser->end_element(empty);
}

int xml_on_text(uint8_t* context, const uint8_t* value, size_t value_len)
{
  ObXmlParser* parser = parser_from_context(context);
  return OB_ISNULL(parser) ? OB_INVALID_ARGUMENT : parser->characters(reinterpret_cast<const char*>(value), value_len);
}

int xml_on_cdata(uint8_t* context, const uint8_t* value, size_t value_len)
{
  ObXmlParser* parser = parser_from_context(context);
  return OB_ISNULL(parser) ? OB_INVALID_ARGUMENT
                           : parser->add_text_node(ObMulModeNodeType::M_CDATA,
                                                   reinterpret_cast<const char*>(value), value_len);
}

int xml_on_comment(uint8_t* context, const uint8_t* value, size_t value_len)
{
  ObXmlParser* parser = parser_from_context(context);
  return OB_ISNULL(parser) ? OB_INVALID_ARGUMENT
                           : parser->add_text_node(ObMulModeNodeType::M_COMMENT,
                                                   reinterpret_cast<const char*>(value), value_len);
}

int xml_on_processing_instruction(uint8_t* context,
                                  const uint8_t* target,
                                  size_t target_len,
                                  const uint8_t* value,
                                  size_t value_len)
{
  ObXmlParser* parser = parser_from_context(context);
  if (OB_ISNULL(parser)) {
    return OB_INVALID_ARGUMENT;
  }
  ObString target_string;
  ObString value_string;
  if (OB_SUCCESS != borrowed_string(target, target_len, target_string)
      || OB_SUCCESS != borrowed_string(value, value_len, value_string)) {
    return OB_INVALID_ARGUMENT;
  }
  return parser->processing_instruction(target_string, value_string);
}

int ObXmlParser::parse_document(const ObString& xml_text)
{
  content_parse_ = false;
  return map_parse_status(xml_parse(reinterpret_cast<const uint8_t*>(xml_text.ptr()),
                                    xml_text.length(), false, options_, reinterpret_cast<uint8_t*>(this)));
}

int ObXmlParser::parse_content(const ObString& xml_text)
{
  content_parse_ = true;
  return map_parse_status(xml_parse(reinterpret_cast<const uint8_t*>(xml_text.ptr()),
                                    xml_text.length(), true, options_, reinterpret_cast<uint8_t*>(this)));
}

int ObXmlParser::check()
{
  INIT_SUCC(ret);
  if (OB_ISNULL(allocator_)) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObXmlParser::set_xml_decl(const ObString& xml_decl_str)
{
  INIT_SUCC(ret);
  ObXmlDocument* document = document_;
  ObIAllocator* allocator = allocator_;
  ObString src_version_str;
  ObString src_encoding_str;
  ObString src_standalone_str;
  char* version_str = nullptr;
  char* encoding_str = nullptr;
  bool has_xml_decl = false;
  bool has_version_value = false;
  bool has_encoding_value = false;
  bool has_standalone_value = false;

  if (content_parse_) {
    return ret;
  }
  if (OB_NOT_NULL(document) && OB_NOT_NULL(allocator)) {
    if ((has_xml_decl = ObXmlParserUtils::has_xml_decl(xml_decl_str))) {
      document->set_has_xml_decl(has_xml_decl);
      if (OB_FAIL(ObXmlParserUtils::parse_xml_decl(xml_decl_str,
                                                   src_version_str,
                                                   has_version_value,
                                                   src_encoding_str,
                                                   has_encoding_value,
                                                   src_standalone_str,
                                                   has_standalone_value))) {
      } else {
        int version_length = src_version_str.length();
        int encoding_length = src_encoding_str.length();
        if (version_length > 0 && OB_ISNULL(version_str = static_cast<char*>(allocator->alloc(version_length)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else if (encoding_length > 0 && OB_ISNULL(encoding_str = static_cast<char*>(allocator->alloc(encoding_length)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          if (version_length > 0) {
            MEMCPY(version_str, src_version_str.ptr(), version_length);
            document->set_version(ObString(version_length, version_str));
          }
          if (encoding_length > 0) {
            MEMCPY(encoding_str, src_encoding_str.ptr(), encoding_length);
            document->set_encoding(ObString(encoding_length, encoding_str));
          }
          document->set_encoding_flag(0 == encoding_length && has_encoding_value);
          document->set_standalone(ObXmlParserUtils::get_standalone_type(src_standalone_str));
        }
      }
    }
  } else {
    ret = OB_INVALID_ARGUMENT;
  }
  return ret;
}

int ObXmlParser::start_document()
{
  INIT_SUCC(ret);
  if (OB_FAIL(this->check())) {
  } else if (OB_ISNULL(document_ = OB_NEWx(ObXmlDocument, allocator_, content_parse_ ? ObMulModeNodeType::M_CONTENT : ObMulModeNodeType::M_DOCUMENT, ctx_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(document_->init())) {
  } else {
    document_->set_flags(MEMBER_LAZY_SORTED);
    if (OB_FAIL(ObXmlParserBase::start_document(document_))) {
    }
  }
  return ret;
}

int ObXmlParser::end_document()
{
  return ObXmlParserBase::end_document();
}

int ObXmlParser::push_namespace(ObXmlAttribute* ns)
{
  INIT_SUCC(ret);
  if (ns_cnt_stack_.size() <= 0 || OB_ISNULL(ns)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ns_cnt_stack_[ns_cnt_stack_.size()-1]++;
    if (OB_FAIL(ns_stack_.push_back(ns))) {
    }
  }
  return ret;
}

int ObXmlParser::pop_namespace()
{
  INIT_SUCC(ret);
  int cur_ns_cnt = 0;
  if (ns_cnt_stack_.size() > 0) {
    if (OB_FAIL(ns_cnt_stack_.pop_back(cur_ns_cnt))) {
    }
    for (int i = 0; OB_SUCC(ret) && i < cur_ns_cnt; ++i) {
      ns_stack_.pop_back();
    }
  }
  return ret;
}

int ObXmlParser::get_namespace(const ObString& name, bool use_default_ns, ObXmlAttribute*& ns)
{
  INIT_SUCC(ret);
  for (int i = ns_stack_.size() - 1; i >= 0; --i) {
    ObXmlAttribute* cur_ns = ns_stack_[i];
    if (cur_ns->get_key().compare(name) == 0
      || (use_default_ns && name.empty() && cur_ns->get_key().compare(ObXmlConstants::XMLNS_STRING) == 0)) {
      ns = cur_ns;
      break;
    }
  }
  if (nullptr == ns && !name.empty() && name.compare(ObXmlConstants::XML_STRING) != 0) {
    ret = OB_ERR_PARSER_SYNTAX;
  }
  return ret;
}

int ObXmlParser::set_element_name(ObXmlElement& element, const ObString& src_name)
{
  INIT_SUCC(ret);
  ObIAllocator* allocator = allocator_;
  char* elem_name = nullptr;
  int32_t elem_name_length = 0;
  ObString qname;
  ObString prefix;
  ObString localname;

  if (OB_ISNULL(allocator)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(src_name.ptr()) && !src_name.empty()) {
    // do nothin ignore
  } else if ((elem_name_length = src_name.length()) > 0) {
    if (src_name.ptr()[0] == ':') {
      ret = OB_ERR_PARSER_SYNTAX;
    } else if (OB_ISNULL(elem_name = static_cast<char*>(allocator->alloc(elem_name_length)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      MEMCPY(elem_name, src_name.ptr(), elem_name_length);
      qname.assign_ptr(elem_name, elem_name_length);
      if (OB_FAIL(ObXmlParserUtils::get_prefix_and_localname(qname, prefix, localname))) {
      } else {
        element.set_prefix(prefix);
        element.set_xml_key(localname);
      }
    }
  }
  return ret;
}

int ObXmlParser::construct_text_value(const ObString &src_attr_value, ObString &attr_value)
{
  INIT_SUCC(ret);
  char *attr_value_ptr = nullptr;
  int attr_value_len = 0;
  attr_value_len = src_attr_value.length();
  if (attr_value_len > 0 && OB_ISNULL(attr_value_ptr = static_cast<char*>(allocator_->alloc(attr_value_len)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (attr_value_len > 0) {
    MEMCPY(attr_value_ptr, src_attr_value.ptr(), attr_value_len);
    attr_value.assign_ptr(attr_value_ptr, attr_value_len);
  }
  return ret;
}

int ObXmlParser::add_element_attr(ObXmlElement& element, const ObString& src_attr_name, const ObString& src_attr_value)
{
  INIT_SUCC(ret);
  ObIAllocator* allocator = allocator_;
  ObXmlAttribute* attr = nullptr;
  char* attr_name = nullptr;
  int64_t attr_name_length = 0;
  int64_t attr_value_length = 0;
  ObString src_attr_value_str;
  ObString attr_value;
  ObString qname;
  ObString prefix;
  ObString localname;

  if (OB_ISNULL(allocator)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(attr = OB_NEWx(ObXmlAttribute, allocator, ObMulModeNodeType::M_ATTRIBUTE, ctx_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    attr_name_length = src_attr_name.length();
    attr_value_length = src_attr_value.length();
    src_attr_value_str = src_attr_value;

    if (OB_SUCC(ret) && attr_name_length > 0) {
      if (src_attr_name[0] == ':') {
        ret = OB_ERR_PARSER_SYNTAX;
      } else if (OB_ISNULL(attr_name = static_cast<char*>(allocator->alloc(attr_name_length)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        MEMCPY(attr_name, src_attr_name.ptr(), attr_name_length);
        qname.assign_ptr(attr_name, attr_name_length);
        if (OB_FAIL(ObXmlParserUtils::get_prefix_and_localname(qname, prefix, localname))) {
        } else {
          attr->set_prefix(prefix);
          attr->set_xml_key(localname);
        }
      }
    }

    if (OB_SUCC(ret) && attr_value_length > 0) {
      if (OB_FAIL(construct_text_value(src_attr_value_str, attr_value))) {
      } else {
        attr->set_value(attr_value);
      }
    }

    if (OB_SUCC(ret)) {
      if (qname.compare("xmlns:") == 0) {
        ret = OB_ERR_PARSER_SYNTAX;
      } else if (ObXmlParserUtils::is_namespace_attribute(attr)) {
        attr->set_xml_type(ObMulModeNodeType::M_NAMESPACE);
        if (this->is_document_parse() && attr_value.empty() && !prefix.empty()) {
          ret = OB_ERR_PARSER_SYNTAX;
        } else if (!prefix.empty() && (localname.compare("xml") == 0 || (localname.compare("xmlns") == 0))) {
          // "xml" and "xmlns" are reserved words and their use is prohibited
          ret = OB_ERR_PARSER_SYNTAX;
        } else if (OB_FAIL(element.add_attribute(attr))) {
        } else if (OB_FAIL(this->push_namespace(attr))) {
        }
      } else if (OB_FAIL(element.add_attribute(attr))) {
      }
    }
  }
  return ret;
}

int ObXmlParser::set_element_namespace(ObXmlElement& element) {
  INIT_SUCC(ret);
  ObXmlAttribute* elem_ns = nullptr;
  ObXmlAttribute* attr_ns = nullptr;
  ObLibContainerNode* attributes = nullptr;

  if (OB_FAIL(this->get_namespace(element.get_prefix(), true, elem_ns))) {
  } else {
    element.set_ns(elem_ns);
    int attr_size = element.attribute_size();
    for (int i = 0; OB_SUCC(ret) && i < attr_size; ++i) {
      ObXmlAttribute* attr = NULL;
      if (OB_FAIL(element.get_attribute(attr, i))) {
      } else if (OB_ISNULL(attr)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (attr->type() == ObMulModeNodeType::M_ATTRIBUTE) {
        if (OB_FAIL(this->get_namespace(attr->get_prefix(), false, attr_ns))) {
        } else {
          attr->set_ns(attr_ns);
          attr_ns = nullptr;
        }
      }
    }
  }
  return ret;
}

int ObXmlParser::start_element(const ObString& name, const XmlAttributeView* attrs, size_t attr_count, bool empty)
{
  INIT_SUCC(ret);
  ObXmlElement* element = nullptr;
  if (OB_FAIL(this->check())) {
  } else if (OB_ISNULL(element = OB_NEWx(ObXmlElement, allocator_, ObMulModeNodeType::M_ELEMENT, ctx_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(element->init())) {
  } else if (FALSE_IT(element->set_flags(MEMBER_LAZY_SORTED))) {
  } else if(OB_FAIL(ns_cnt_stack_.push_back(0))) {
  } else if (OB_FAIL(this->set_element_name(*element, name))) {
  }

  for (size_t i = 0; OB_SUCC(ret) && i < attr_count; ++i) {
    ObString attr_name;
    ObString attr_value;
    if (OB_FAIL(borrowed_string(attrs[i].name, attrs[i].name_len, attr_name))) {
    } else if (OB_FAIL(borrowed_string(attrs[i].value, attrs[i].value_len, attr_value))) {
    } else if (OB_FAIL(this->add_element_attr(*element, attr_name, attr_value))) {
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(this->set_element_namespace(*element))) {
    } else if (OB_FAIL(ObXmlParserBase::start_element(element))) {
    }
  }

  return ret;
}

int ObXmlParser::end_element(bool empty)
{
  INIT_SUCC(ret);
  ObXmlElement* element = nullptr;
  if (OB_FAIL(this->check())) {
  } else if (OB_ISNULL(element =
                ObXmlUtil::xml_node_cast<ObXmlElement>(cur_node_, ObMulModeNodeType::M_ELEMENT))) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    element->set_empty(empty);
    if (OB_FAIL(ObXmlParserBase::end_element())) {
    } else if (OB_FAIL(this->pop_namespace())) {
    }
  }
  return ret;
}

int ObXmlParser::alloc_text_node(ObMulModeNodeType type,
                                        const char* src_value,
                                        int value_len,
                                        ObXmlText*& node)
{
  INIT_SUCC(ret);
  char* str = nullptr;
  if (OB_FAIL(this->check())) {
  } else if (OB_ISNULL(node = OB_NEWx(ObXmlText, allocator_, type, ctx_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (value_len > 0) {
    if (OB_ISNULL(str = static_cast<char*>(allocator_->alloc(value_len)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      MEMCPY(str, src_value, value_len);
      node->set_value(ObString(value_len, str));
    }
  }
  return ret;
}

int ObXmlParser::add_text_node(ObMulModeNodeType type, const char* value, int len)
{
  INIT_SUCC(ret);
  ObXmlText* cdata = nullptr;
  if (OB_FAIL(this->check())) {
  } else if (OB_FAIL(this->alloc_text_node(type,
                                           value,
                                           len,
                                           cdata))) {
  } else if (OB_FAIL(ObXmlParserBase::add_text_node(cdata))) {
  }
  return ret;
}

int ObXmlParser::characters(const char *ch, int len)
{
  INIT_SUCC(ret);
  if (OB_FAIL(this->check())) {
  } else if (len > 0 && OB_ISNULL(ch)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(this->add_or_merge_text(ObString(len, ch)))) {
  }
  return ret;
}

int ObXmlParser::processing_instruction(const ObString& target, const ObString& data)
{
  INIT_SUCC(ret);
  ObXmlAttribute* pi = nullptr;
  const char *src_target = target.ptr();
  const char *src_data = data.ptr();
  int name_len = target.length();
  int value_len = data.length();
  char* name = nullptr;
  char* value = nullptr;

  if (OB_FAIL(this->check())) {
  } else if (OB_ISNULL(pi = OB_NEWx(ObXmlAttribute, allocator_, ObMulModeNodeType::M_INSTRUCT, ctx_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (name_len > 0 && OB_ISNULL(name = static_cast<char*>(allocator_->alloc(name_len)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (value_len > 0 && OB_ISNULL(value = static_cast<char*>(allocator_->alloc(value_len)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    if (name_len > 0)MEMCPY(name, src_target, name_len);
    if (value_len > 0)MEMCPY(value, src_data, value_len);
    pi->set_xml_key(ObString(name_len, name));
    pi->set_value(ObString(value_len, value));
    if (OB_FAIL(ObXmlParserBase::processing_instruction(pi))) {
    }
  }
  return ret;
}

} // namespace common
} // namespace oceanbase
