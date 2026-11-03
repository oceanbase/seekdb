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

// Extracted from ObSrsWktParser: identical WKT1 productions, independent of
// host containers. Model supplies Fusion-adapted records and checked string
// assignment. Private source-level implementation, never installed as C ABI.
#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/phoenix.hpp>
#include <memory>
#include <string>
#include <string_view>

namespace seekdb::geo::srs {
namespace qi = boost::spirit::qi;
using boost::spirit::double_;

template <typename Model, typename Iterator, typename Skipper>
struct SrsWktGrammar : qi::grammar<Iterator, typename Model::CoordinateSystem(), Skipper>
{
  using Geographic = typename Model::Geographic;
  using Projected = typename Model::Projected;
  using CoordinateSystem = typename Model::CoordinateSystem;
  using Datum = typename Model::Datum;
  using Spheroid = typename Model::Spheroid;
  using Authority = typename Model::Authority;
  using Towgs84 = typename Model::Towgs84;
  using AxisPair = typename Model::AxisPair;
  using Axis = typename Model::Axis;
  using PrimeMeridian = typename Model::PrimeMeridian;
  using Unit = typename Model::Unit;
  using Parameters = typename Model::Parameters;
  using Parameter = typename Model::Parameter;
  using Projection = typename Model::Projection;
  using Direction = typename Model::Direction;
  using String = typename Model::String;
  SrsWktGrammar(char l_brac, char r_brac, Model &model) : SrsWktGrammar::base_type(start_)
  {
    // rules definition
    l_brac_ = qi::lit(l_brac);
    r_brac_ = qi::lit(r_brac);
    comma_ = qi::lit(',');
    geog_lit_ = qi::no_case[qi::lit("GEOGCS")];
    datum_lit_ = qi::no_case[qi::lit("DATUM")];
    spher_lit_ = qi::no_case[qi::lit("SPHEROID")];
    towgs_lit_ = qi::no_case[qi::lit("TOWGS84")];
    auth_lit_= qi::no_case[qi::lit("AUTHORITY")]; 
    prim_lit_ = qi::no_case[qi::lit("PRIMEM")];
    unit_lit_= qi::no_case[qi::lit("UNIT")];
    axis_lit_ = qi::no_case[qi::lit("AXIS")];
    proj_lit_ = qi::no_case[qi::lit("PROJECTION")];
    proj_pram_lit_ = qi::no_case[qi::lit("PARAMETER")];
    proj_rs_lit_ = qi::no_case[qi::lit("PROJCS")];

    q_str_ = '"' >> *~qi::char_('"') >> '"';
    q_obstr_ = q_str_[qi::_pass = boost::phoenix::bind(
        [&model](String &out, const std::string &value) { return model.assign(out, value); },
        qi::_val, qi::_1)];

    // must be lower case to be insensitive.
    direct_symbol_.add("east", Direction::EAST)("south", Direction::SOUTH)(
                       "west", Direction::WEST)("north", Direction::NORTH)(
                       "other", Direction::OTHER);
    direct_ = qi::no_case[direct_symbol_];

    auth_ = auth_lit_ >> qi::attr(true) >> l_brac_ >>
            q_obstr_ >> comma_ >>
            q_obstr_ >> r_brac_;

    towgs_ = towgs_lit_ >> qi::attr(true) >> l_brac_ >>
             double_ >> comma_ >>
             double_ >> comma_ >>
             double_ >> comma_ >>
             double_ >> comma_ >>
             double_ >> comma_ >>
             double_ >> comma_ >>
             double_ >> r_brac_;

    spher_ = spher_lit_ >> l_brac_ >> q_obstr_ >> 
             comma_ >> double_ >>
             comma_ >> double_ >>
             -(comma_ >> auth_) >> r_brac; 

    prim_ = prim_lit_ >> l_brac_ >>
            q_obstr_ >> comma_ >>
            double_ >> -(comma_ >> auth_) >> r_brac_;

    datum_ = datum_lit_ >> l_brac_ >>
             q_obstr_ >> comma_ >>
             spher_ >> -(comma_ >> towgs_) >>
             -(comma_ >> auth_) >> r_brac;

    unit_ = unit_lit_ >> l_brac_ >>
            q_obstr_ >> comma_ >>
            double_ >> -(comma_ >> auth_) >> r_brac;

    axis_ = axis_lit_ >> l_brac_ >>
            q_obstr_ >> comma_ >>
            direct_ >> r_brac_;
            
    axis_pair_ = axis_ >> comma_ >> axis_;

    geog_rs_ = geog_lit_ >> l_brac_ >>
               q_obstr_ >> comma_ >>
               datum_ >> comma_ >>
               prim_ >> comma_ >>
               unit_ >> comma_ >>
               axis_pair_ >> -(comma_ >> auth_) >> r_brac_; 

    proj_ = proj_lit_ >> l_brac_ >>
            q_obstr_ >> -(comma_ >> auth_) >> r_brac_;

    proj_param_ = proj_pram_lit_ >> l_brac_ >>
                  q_obstr_ >> comma_ >>
                  double_ >> -(comma_ >> auth_) >> r_brac_;

    proj_params_ = proj_param_ % comma_;

    proj_rs_ = proj_rs_lit_ >> l_brac >>
               q_obstr_ >> comma_ >>
               geog_rs_ >> comma_ >>
               proj_ >> -(comma_ >> proj_params_) >> comma_ >>
               unit_ >> -(comma_ >> axis_pair_) >>
               -(comma_ >> auth_) >> r_brac_;

    start_ = proj_rs_ | geog_rs_;
  }

  // rules declaration
  qi::rule<Iterator> l_brac_;
  qi::rule<Iterator> r_brac_;
  qi::rule<Iterator> comma_;
  qi::rule<Iterator> geog_lit_;
  qi::rule<Iterator> datum_lit_;
  qi::rule<Iterator> spher_lit_;
  qi::rule<Iterator> auth_lit_; 
  qi::rule<Iterator> prim_lit_;
  qi::rule<Iterator> unit_lit_;
  qi::rule<Iterator> axis_lit_;
  qi::rule<Iterator> towgs_lit_;
  qi::rule<Iterator> proj_lit_;
  qi::rule<Iterator> proj_pram_lit_;
  qi::rule<Iterator> proj_rs_lit_;
  qi::rule<Iterator, std::string()> q_str_; 
  qi::rule<Iterator, String()> q_obstr_;
  qi::symbols<char,  Direction> direct_symbol_;
  qi::rule<Iterator, Direction> direct_;

  qi::rule<Iterator, Authority(), Skipper> auth_;
  qi::rule<Iterator, Towgs84(), Skipper> towgs_;
  qi::rule<Iterator, Datum(), Skipper> datum_;
  qi::rule<Iterator, PrimeMeridian(), Skipper> prim_;
  qi::rule<Iterator, Unit(), Skipper> unit_;
  qi::rule<Iterator, Axis(), Skipper> axis_;
  qi::rule<Iterator, AxisPair(), Skipper> axis_pair_;
  qi::rule<Iterator, Spheroid(), Skipper> spher_;
  qi::rule<Iterator, Projection(), Skipper> proj_;
  qi::rule<Iterator, Parameter(), Skipper> proj_param_;
  qi::rule<Iterator, Parameters(), Skipper> proj_params_;

  qi::rule<Iterator, Projected(), Skipper> proj_rs_;
  qi::rule<Iterator, Geographic(), Skipper> geog_rs_;
  qi::rule<Iterator, CoordinateSystem(), Skipper> start_;
};

inline bool ascii_space(char c)
{
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

// Caller bounds input according to its own catalog/service contract. Grammar
// depth is fixed (not arbitrary recursive WKT). Allocation exceptions remain
// inside the C++ caller, which must translate them before crossing a C ABI.
template <typename Model>
bool parse_wkt(std::string_view input, Model &model, typename Model::CoordinateSystem &out)
{
  while (!input.empty() && ascii_space(input.back())) input.remove_suffix(1);
  if (input.empty() || (input.back() != ')' && input.back() != ']')) return false;
  const char right = input.back();
  const char *begin = input.data();
  const char *end = begin + input.size();
  auto parser = std::make_unique<SrsWktGrammar<Model, const char *, boost::spirit::ascii::space_type>>(
      right == ')' ? '(' : '[', right, model);
  typename Model::CoordinateSystem parsed;
  if (!qi::phrase_parse(begin, end, *parser, boost::spirit::ascii::space, parsed) || begin != end) return false;
  out = std::move(parsed);
  return true;
}
} // namespace seekdb::geo::srs

