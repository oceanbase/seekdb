/*
 * Experimental native file-table SQL adapter for seekdb 1.4.
 *
 * The adapter rewrites FROM/JOIN read_csv/read_json/read_ndjson/glob into
 * JSON_TABLE source nodes, so relational operations remain inside seekdb.
 * It is deliberately bounded (2 MiB JSON materialization), not the final
 * streaming/Parquet/COPY implementation.
 *
 * Access is opt-in: all resolved files must be below SEEKDB_FILE_SQL_ROOT,
 * or /tmp/seekdb-file-sql when the variable is unset. Canonical paths are
 * checked with realpath() to block symlink and .. escapes.
 */
#ifndef OCEANBASE_SQL_PARSER_OB_FILE_SQL_REWRITE_H_
#define OCEANBASE_SQL_PARSER_OB_FILE_SQL_REWRITE_H_

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <glob.h>
#include <limits.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace oceanbase {
namespace sql {
namespace native_file_sql {

static const size_t MAX_FILE_BYTES = 2 * 1024 * 1024;
static const size_t MAX_RESULT_BYTES = 2 * 1024 * 1024;
static const size_t MAX_FILE_ROWS = 10000;

inline bool space(const char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
inline std::string lower(const std::string &x) {
  std::string out=x;
  for (size_t i=0; i<out.size(); ++i) {
    if (out[i]>='A' && out[i]<='Z') out[i]=static_cast<char>(out[i]-'A'+'a');
  }
  return out;
}
inline std::string trim(const std::string &x) {
  size_t a=0,b=x.size();
  while (a<b && space(x[a])) ++a;
  while (b>a && space(x[b-1])) --b;
  return x.substr(a,b-a);
}
inline bool id_start(const char c) {
  return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';
}
inline bool id_char(const char c) {
  return id_start(c)||(c>='0'&&c<='9');
}
inline std::string json_quote(const std::string &x) {
  static const char *hex="0123456789abcdef";
  std::string out="\"";
  for (size_t i=0; i<x.size(); ++i) {
    unsigned char c=static_cast<unsigned char>(x[i]);
    switch(c) {
      case '"': out+="\\\""; break;
      case '\\': out+="\\\\"; break;
      case '\n': out+="\\n"; break;
      case '\r': out+="\\r"; break;
      case '\t': out+="\\t"; break;
      default:
        if (c<32) {
          out+="\\u00";
          out.push_back(hex[c>>4]);
          out.push_back(hex[c&15]);
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  out.push_back('"');
  return out;
}
inline std::string sql_quote(const std::string &x) {
  // Parser uses MySQL SQL mode, in which backslash must be escaped.
  std::string out="'";
  for (size_t i=0; i<x.size(); ++i) {
    char c=x[i];
    if (c=='\'') out+="''";
    else if (c=='\\') out+="\\\\";
    else out.push_back(c);
  }
  out.push_back('\'');
  return out;
}
inline std::string sql_identifier(const std::string &x) {
  std::string out="`";
  for (size_t i=0;i<x.size();++i) {
    if (x[i]=='`') out+="``";
    else out.push_back(x[i]);
  }
  out.push_back('`');
  return out;
}
inline bool parse_quoted_path(const std::string &sql,size_t &pos,std::string &path) {
  if (pos>=sql.size() || sql[pos]!='\'') return false;
  ++pos;
  for (;pos<sql.size();++pos) {
    char c=sql[pos];
    if (c=='\'') {
      if (pos+1<sql.size() && sql[pos+1]=='\'') {
        path.push_back('\'');
        ++pos;
      } else {
        ++pos;
        return true;
      }
    } else if (c=='\\' && pos+1<sql.size()) {
      // Handle escaped quote/backslash and standard MySQL path literal.
      path.push_back(sql[++pos]);
    } else {
      path.push_back(c);
    }
  }
  return false;
}
inline bool permitted_root(std::string &root,std::string &err) {
  const char *env=std::getenv("SEEKDB_FILE_SQL_ROOT");
  const char *want=env && *env ? env : "/tmp/seekdb-file-sql";
  char resolved[PATH_MAX];
  if (::realpath(want,resolved)==nullptr) {
    err=std::string("file_sql_root does not exist: ")+want;
    return false;
  }
  struct stat st;
  if (::stat(resolved,&st)!=0 || !S_ISDIR(st.st_mode)) {
    err="file_sql_root is not a directory";
    return false;
  }
  root=resolved;
  return true;
}
inline bool under_root(const std::string &root,const std::string &candidate) {
  return candidate.size()>root.size() &&
      candidate.compare(0,root.size(),root)==0 &&
      (root=="/" || candidate[root.size()]=='/');
}
inline bool resolve_file(const std::string &input,std::string &path,std::string &err) {
  std::string root;
  if (!permitted_root(root,err)) return false;
  // No remote URLs, named pipes, directories, or special devices.
  if (input.empty() || input.find('\0')!=std::string::npos ||
      input.find("://")!=std::string::npos) {
    err="invalid local file path";
    return false;
  }
  const std::string original=input[0]=='/'?input:(root+"/"+input);
  char resolved[PATH_MAX];
  if (::realpath(original.c_str(),resolved)==nullptr) {
    err="file cannot be resolved within file_sql_root";
    return false;
  }
  path=resolved;
  struct stat st;
  if (!under_root(root,path) || ::stat(path.c_str(),&st)!=0 ||
      !S_ISREG(st.st_mode) || st.st_size<0 ||
      static_cast<size_t>(st.st_size)>MAX_FILE_BYTES) {
    err="read requires a regular file smaller than 2 MiB inside file_sql_root";
    return false;
  }
  return true;
}
inline bool read_text(const std::string &input,std::string &text,std::string &err) {
  std::string path;
  if (!resolve_file(input,path,err)) return false;
  std::ifstream file(path.c_str(),std::ios::binary);
  if (!file.good()) { err="cannot open file"; return false; }
  std::ostringstream buffer;
  buffer<<file.rdbuf();
  text=buffer.str();
  if (text.size()>MAX_FILE_BYTES) {err="file exceeds size limit"; return false;}
  return file.good() || file.eof();
}
inline bool json_balanced_object(const std::string &x,size_t a,size_t &b) {
  if (a>=x.size() || x[a]!='{') return false;
  bool quoted=false,escape=false;
  int depth=0;
  for (size_t i=a;i<x.size();++i) {
    char c=x[i];
    if (quoted) {
      if (escape) escape=false;
      else if (c=='\\') escape=true;
      else if (c=='"') quoted=false;
    } else if(c=='"') quoted=true;
    else if(c=='{' || c=='[') ++depth;
    else if(c=='}' || c==']') {
      --depth;
      if (depth==0) {b=i+1;return true;}
      if (depth<0) return false;
    }
  }
  return false;
}
inline bool json_string_at(const std::string &x,size_t &p,std::string &value) {
  if (p>=x.size() || x[p]!='"') return false;
  ++p;
  bool escape=false;
  for (;p<x.size();++p) {
    char c=x[p];
    if (escape) {
      if (c=='n') value.push_back('\n');
      else if (c=='r') value.push_back('\r');
      else if (c=='t') value.push_back('\t');
      else value.push_back(c);
      escape=false;
    } else if (c=='\\') escape=true;
    else if(c=='"') {++p;return true;}
    else value.push_back(c);
  }
  return false;
}
inline bool json_object_keys(const std::string &obj,std::vector<std::string> &keys) {
  if (obj.empty() || obj[0]!='{') return false;
  int depth=0;
  bool quote=false, escape=false;
  for (size_t i=0;i<obj.size();++i) {
    char c=obj[i];
    if (quote) {
      if (escape) escape=false;
      else if(c=='\\') escape=true;
      else if(c=='"') quote=false;
      continue;
    }
    if(c=='{') {++depth;continue;}
    if(c=='}') {--depth;continue;}
    if(c=='[') {++depth;continue;}
    if(c==']') {--depth;continue;}
    if(c=='"' && depth==1) {
      size_t end=i;
      std::string name;
      if (!json_string_at(obj,end,name)) return false;
      size_t after=end;
      while(after<obj.size() && space(obj[after]))++after;
      if (after<obj.size() && obj[after]==':') {
        if (std::find(keys.begin(),keys.end(),name)==keys.end()) keys.push_back(name);
      }
      i=end-1;
      continue;
    }
    if (c=='"') quote=true;
  }
  return !keys.empty() && depth==0;
}
inline bool parse_json_records(const std::string &input,bool ndjson,
                               std::vector<std::string> &objects,
                               std::vector<std::string> &names,std::string &err) {
  if (ndjson) {
    std::istringstream lines(input);
    std::string line;
    while(std::getline(lines,line)) {
      line=trim(line);
      if (line.empty())continue;
      size_t e=0;
      if (!json_balanced_object(line,0,e) || e!=line.size()) {
        err="NDJSON requires one JSON object per line";
        return false;
      }
      objects.push_back(line);
    }
  } else {
    std::string doc=trim(input);
    if (doc.empty()) {err="JSON input is empty";return false;}
    if (doc[0]=='{') {
      size_t end=0;
      if (!json_balanced_object(doc,0,end) || end!=doc.size()) {
        err="malformed JSON object";return false;
      }
      objects.push_back(doc);
    } else if(doc[0]=='[' && doc.back()==']') {
      size_t pos=1;
      while(pos+1<doc.size()) {
        while (pos+1<doc.size() && (space(doc[pos]) || doc[pos]==',')) ++pos;
        if (pos+1>=doc.size())break;
        size_t end=0;
        if (!json_balanced_object(doc,pos,end)) {
          err="JSON arrays must contain objects";return false;
        }
        objects.push_back(doc.substr(pos,end-pos));
        pos=end;
        while(pos+1<doc.size() && space(doc[pos]))++pos;
        if(pos+1<doc.size() && doc[pos]!=',') {
          err="invalid JSON array separator";return false;
        }
      }
    } else {err="read_json expects JSON object or array";return false;}
  }
  if (objects.size()>MAX_FILE_ROWS) {err="too many JSON rows";return false;}
  for (size_t i=0;i<objects.size();++i) {
    if (!json_object_keys(objects[i],names)) {
      err="cannot infer JSON object keys";
      return false;
    }
  }
  if (names.empty()) names.push_back("value");
  return true;
}
inline bool csv_rows(const std::string &data,
                     std::vector<std::vector<std::string> > &rows,std::string &err) {
  bool quote=false,at_start=true;
  std::string cell;
  std::vector<std::string> row;
  size_t start=(data.size()>=3 &&
       static_cast<unsigned char>(data[0])==0xef &&
       static_cast<unsigned char>(data[1])==0xbb &&
       static_cast<unsigned char>(data[2])==0xbf)?3:0;
  for (size_t i=start;i<data.size();++i) {
    char c=data[i];
    if(quote) {
      if(c=='"') {
        if(i+1<data.size() && data[i+1]=='"') {cell.push_back('"');++i;}
        else quote=false;
      } else cell.push_back(c);
    } else if(c=='"' && at_start) {
      quote=true;
      at_start=false;
    } else if(c==',') {
      row.push_back(cell);cell.clear();at_start=true;
    } else if(c=='\n' || c=='\r') {
      if(c=='\r' && i+1<data.size() && data[i+1]=='\n')++i;
      row.push_back(cell); cell.clear();at_start=true;
      rows.push_back(row);row.clear();
      if(rows.size()>MAX_FILE_ROWS+1){err="too many CSV rows";return false;}
    } else {
      cell.push_back(c);at_start=false;
    }
  }
  if(quote){err="unterminated CSV quote";return false;}
  if(!cell.empty() || !row.empty()){
    row.push_back(cell);rows.push_back(row);
  }
  return true;
}
inline bool is_number(const std::string &value) {
  // RFC 8259 number grammar. In particular, 001, +1 and 1. must be
  // represented as strings, not emitted as invalid JSON numeric tokens.
  size_t i=0;
  if (i<value.size() && value[i]=='-') ++i;
  if (i==value.size()) return false;
  if (value[i]=='0') {
    ++i;
  } else if (value[i]>='1' && value[i]<='9') {
    do {++i;} while(i<value.size() && value[i]>='0' && value[i]<='9');
  } else return false;
  if (i<value.size() && value[i]=='.') {
    ++i;
    size_t start=i;
    while(i<value.size() && value[i]>='0' && value[i]<='9')++i;
    if(i==start)return false;
  }
  if (i<value.size() && (value[i]=='e' || value[i]=='E')) {
    ++i;
    if(i<value.size() && (value[i]=='+' || value[i]=='-'))++i;
    size_t start=i;
    while(i<value.size() && value[i]>='0' && value[i]<='9')++i;
    if(i==start)return false;
  }
  if(i!=value.size())return false;
  errno=0;
  char *end=nullptr;
  const double n=std::strtod(value.c_str(),&end);
  return errno==0 && end==value.c_str()+value.size() && std::isfinite(n);
}
inline bool csv_to_json(const std::string &data,std::string &doc,
                        std::vector<std::string> &names,
                        std::vector<std::string> &types,std::string &err) {
  std::vector<std::vector<std::string> > rows;
  if(!csv_rows(data,rows,err))return false;
  if(rows.empty()) {err="CSV header is missing";return false;}
  names=rows.front();
  if(names.empty()) {err="CSV column list is empty";return false;}
  for(size_t i=0;i<names.size();++i) {
    if(names[i].empty()) names[i]="column_"+std::to_string(i+1);
    // Deduplicate names deterministically while preserving their SQL order.
    const std::string base=names[i];
    unsigned n=2;
    for(size_t j=0;j<i;++j) {
      if(names[j]==names[i]) {
        names[i]=base+"_"+std::to_string(n++);
        j=static_cast<size_t>(-1);
      }
    }
  }
  types.assign(names.size(),"BIGINT");
  for(size_t r=1;r<rows.size();++r) {
    if(rows[r].size()!=names.size()) {
      err="CSV row has different column count";return false;
    }
    for(size_t c=0;c<names.size();++c) {
      const std::string &v=rows[r][c];
      if(v.empty()) continue;
      if(!is_number(v)) types[c]="VARCHAR(4096)";
      else if(types[c]=="BIGINT" && v.find_first_of(".eE")!=std::string::npos)
        types[c]="DOUBLE";
    }
  }
  doc="[";
  for(size_t r=1;r<rows.size();++r) {
    if(r>1)doc+=",";
    doc+="{";
    for(size_t c=0;c<names.size();++c) {
      if(c)doc+=",";
      doc+=json_quote(names[c])+":";
      const std::string &value=rows[r][c];
      if(value.empty())doc+="null";
      else if(types[c]!="VARCHAR(4096)" && is_number(value))doc+=value;
      else doc+=json_quote(value);
    }
    doc+="}";
    if(doc.size()>MAX_RESULT_BYTES){err="JSON result exceeds 2 MiB";return false;}
  }
  doc+="]";
  return true;
}
inline bool glob_to_json(const std::string &input,std::string &doc,std::string &err) {
  std::string root;
  if(!permitted_root(root,err))return false;
  if(input.find("..")!=std::string::npos || input.find("://")!=std::string::npos) {
    err="glob patterns cannot traverse outside file_sql_root";return false;
  }
  const std::string pattern=input.empty()?"":(input[0]=='/'?input:root+"/"+input);
  if(pattern.empty() || !under_root(root,pattern)) {
    err="glob pattern is outside file_sql_root";return false;
  }
  glob_t matches={};
  int rc=::glob(pattern.c_str(),0,nullptr,&matches);
  if(rc!=0 && rc!=GLOB_NOMATCH) {
    ::globfree(&matches);err="glob() failed";return false;
  }
  if(matches.gl_pathc>MAX_FILE_ROWS) {
    ::globfree(&matches);err="too many glob matches";return false;
  }
  doc="[";
  for(size_t i=0;i<matches.gl_pathc;++i) {
    std::string resolved;
    std::string path_error;
    if(!resolve_file(matches.gl_pathv[i],resolved,path_error)) {
      ::globfree(&matches);err="glob result violates file_sql_root";return false;
    }
    if(i)doc+=",";
    doc+="{\"file\":"+json_quote(resolved)+"}";
    if(doc.size()>MAX_RESULT_BYTES){
      ::globfree(&matches);err="glob result exceeds 2 MiB";return false;
    }
  }
  ::globfree(&matches);
  doc+="]";
  return true;
}
inline bool source_sql(const std::string &type,const std::string &path,
                       std::string &target,std::string &err) {
  std::string doc;
  std::vector<std::string> names,types;
  if(type=="glob") {
    if(!glob_to_json(path,doc,err))return false;
    names.push_back("file");
    types.push_back("VARCHAR(4096)");
  } else {
    std::string data;
    if(!read_text(path,data,err))return false;
    if(type=="read_csv") {
      if(!csv_to_json(data,doc,names,types,err))return false;
    } else if(type=="read_json" || type=="read_ndjson") {
      std::vector<std::string> objects;
      if(!parse_json_records(data,type=="read_ndjson",objects,names,err))
        return false;
      doc="[";
      for(size_t i=0;i<objects.size();++i) {
        if(i)doc+=",";
        doc+=objects[i];
        if(doc.size()>MAX_RESULT_BYTES) {
          err="JSON result exceeds 2 MiB";return false;
        }
      }
      doc+="]";
      types.assign(names.size(),"VARCHAR(4096)");
    } else {
      err="read_parquet requires an Arrow/Parquet scan integration (not implemented)";
      return false;
    }
  }
  if(doc.size()>MAX_RESULT_BYTES){err="source result exceeds 2 MiB";return false;}
  target="JSON_TABLE("+sql_quote(doc)+", '$[*]' COLUMNS (";
  for(size_t i=0;i<names.size();++i) {
    if(i)target+=", ";
    // "$.quoted-key" is a valid JSON path in OceanBase's JSON_TABLE.
    std::string path_expr="$."+json_quote(names[i]);
    target+=sql_identifier(names[i])+" "+types[i]+" PATH "+sql_quote(path_expr);
  }
  target+="))";
  return true;
}

// After rewriting a file table function to JSON_TABLE, supply a generated
// table alias only when the original query omitted one. JSON_TABLE requires it.
inline bool file_table_needs_alias(const std::string &sql, size_t pos) {
  for (;;) {
    while (pos < sql.size() && space(sql[pos])) ++pos;
    if (pos + 1 < sql.size() && sql[pos]=='/' && sql[pos+1]=='*') {
      const size_t end = sql.find("*/", pos+2);
      if (end == std::string::npos) return false;
      pos = end+2;
    } else if (pos + 1 < sql.size() && sql[pos]=='-' && sql[pos+1]=='-' &&
               (pos+2==sql.size() || space(sql[pos+2]))) {
      const size_t end=sql.find('\n',pos+2);
      pos = end==std::string::npos?sql.size():end+1;
    } else if (pos < sql.size() && sql[pos]=='#') {
      const size_t end=sql.find('\n',pos+1);
      pos = end==std::string::npos?sql.size():end+1;
    } else {
      break;
    }
  }
  if (pos==sql.size()) return true;
  if (sql[pos]==';' || sql[pos]==',' || sql[pos]==')') return true;
  if (!id_start(sql[pos])) return false;
  size_t end=pos+1;
  while(end<sql.size() && id_char(sql[end])) ++end;
  const std::string word=lower(sql.substr(pos,end-pos));
  const char *keywords[]={
    "where", "group", "order", "having", "limit", "union", "intersect",
    "except", "join", "inner", "left", "right", "full", "outer", "cross",
    "natural", "straight_join", "on", "using", "window", "qualify", "for", "lock"
  };
  for (size_t k=0;k<sizeof(keywords)/sizeof(keywords[0]);++k) {
    if (word==keywords[k]) return true;
  }
  return false;
}


// COPY is deliberately lowered to the seekdb native SELECT INTO OUTFILE /
// INSERT ... SELECT operators, never to a separate file-writing SQL engine.
// Unlike the file reader, output must not exist and its resolved PARENT
// directory must stay within the configured allowed directory.
inline bool copy_output_path(const std::string &input,std::string &output,
                             std::string &err) {
  std::string root;
  if (!permitted_root(root,err)) return false;
  if (input.empty() || input.find('\0')!=std::string::npos ||
      input.find("://")!=std::string::npos) {
    err="COPY TO requires a local file path";
    return false;
  }
  const std::string absolute=input[0]=='/'?input:root+"/"+input;
  const size_t sep=absolute.rfind('/');
  if (sep==std::string::npos || sep+1>=absolute.size()) {
    err="COPY TO requires a named file";
    return false;
  }
  char parent[PATH_MAX];
  const std::string dirname=sep==0?"/":absolute.substr(0,sep);
  if (::realpath(dirname.c_str(),parent)==nullptr) {
    err="COPY TO parent directory cannot be resolved";
    return false;
  }
  const std::string resolved_parent(parent);
  if (resolved_parent!=root && !under_root(root,resolved_parent)) {
    err="COPY TO path is outside file_sql_root";
    return false;
  }
  const std::string basename=absolute.substr(sep+1);
  if (basename=="." || basename==".." || basename.find('/')!=std::string::npos) {
    err="invalid COPY TO filename";
    return false;
  }
  output=resolved_parent+(resolved_parent=="/"?"":"/")+basename;
  // Reject existing files, including symlinks and special files. The SQL
  // executor must ALSO use an atomic create-without-replacement operation;
  // this preflight check alone cannot eliminate rename races.
  struct stat st;
  if (::lstat(output.c_str(),&st)==0) {
    err="COPY TO refuses to overwrite an existing file";
    return false;
  }
  if (errno!=ENOENT) {
    err="COPY TO cannot inspect target";
    return false;
  }
  return true;
}
inline void copy_skip_ws(const std::string &s,size_t &p) {
  while(p<s.size() && space(s[p])) ++p;
}
inline bool copy_word(const std::string &s,size_t &p,const char *literal) {
  copy_skip_ws(s,p);
  const size_t n=std::strlen(literal);
  if (p+n>s.size() || lower(s.substr(p,n))!=literal ||
      (p+n<s.size() && id_char(s[p+n]))) return false;
  p+=n;
  return true;
}
inline bool copy_parenthesized_query(const std::string &s,size_t &p,
                                     std::string &query,std::string &err) {
  if(p>=s.size() || s[p]!='(') {
    err="COPY expected parenthesized query";
    return false;
  }
  const size_t start=++p;
  int depth=1;
  char quote=0;
  for(;p<s.size();++p) {
    const char c=s[p];
    if(quote!=0) {
      if(c=='\\' && p+1<s.size()) ++p;
      else if(c==quote && p+1<s.size() && s[p+1]==quote) ++p;
      else if(c==quote) quote=0;
    } else if(c=='\'' || c=='"' || c=='`') quote=c;
    else if(c=='(') ++depth;
    else if(c==')') {
      if(--depth==0) {
        query=trim(s.substr(start,p-start));
        ++p;
        const std::string ql=lower(query);
        if((ql.compare(0,6,"select")!=0 || (query.size()>6 && id_char(query[6]))) &&
           (ql.compare(0,4,"with")!=0 || (query.size()>4 && id_char(query[4])))) {
          err="COPY accepts only SELECT / WITH queries";
          return false;
        }
        if(query.find(';')!=std::string::npos) {
          err="COPY cannot contain SQL statement separators";
          return false;
        }
        return true;
      }
    }
  }
  err="unbalanced COPY query";
  return false;
}
inline bool copy_table_name(const std::string &s,size_t &p,
                            std::string &table,std::string &err) {
  const size_t start=p;
  bool in_quote=false;
  while(p<s.size()) {
    const char c=s[p];
    if(c=='`') {
      if(in_quote && p+1<s.size() && s[p+1]=='`') {p+=2;continue;}
      in_quote=!in_quote;
      ++p;
    } else if(!in_quote && space(c))break;
    else if(!in_quote && !(id_char(c) || c=='.'))break;
    else ++p;
  }
  table=s.substr(start,p-start);
  if(table.empty() || in_quote || table.find(';')!=std::string::npos) {
    err="invalid COPY table identifier";
    return false;
  }
  return true;
}
inline bool copy_options(const std::string &s,size_t &p,std::string &err) {
  copy_skip_ws(s,p);
  if (copy_word(s,p,"with"))copy_skip_ws(s,p);
  if(p<s.size() && s[p]=='(') {
    ++p;
    for(;;) {
      copy_skip_ws(s,p);
      if(p<s.size() && s[p]==')') {++p;break;}
      const size_t start=p;
      while(p<s.size() && id_char(s[p]))++p;
      const std::string name=lower(s.substr(start,p-start));
      copy_skip_ws(s,p);
      if(p<s.size() && s[p]=='=') {++p;copy_skip_ws(s,p);}
      if(name=="format") {
        if(!copy_word(s,p,"csv")) {
          err="COPY currently supports CSV format only";
          return false;
        }
      } else if(name=="header") {
        if(copy_word(s,p,"false") || copy_word(s,p,"off")) {
        } else {
          err="COPY HEADER TRUE is not yet supported by seekdb native exporter";
          return false;
        }
      } else if(name=="delimiter") {
        std::string delimiter;
        if(!parse_quoted_path(s,p,delimiter) || delimiter!=",") {
          err="COPY currently requires comma delimiter";
          return false;
        }
      } else {
        err="unsupported COPY option: "+name;
        return false;
      }
      copy_skip_ws(s,p);
      if(p<s.size() && s[p]==',') ++p;
      else if(p<s.size() && s[p]!=')') {
        err="invalid COPY option separator";
        return false;
      }
    }
  } else if (copy_word(s,p,"format")) {
    if(!copy_word(s,p,"csv")) {
      err="COPY currently supports CSV format only";
      return false;
    }
  }
  copy_skip_ws(s,p);
  if(p<s.size() && s[p]==';') {++p;copy_skip_ws(s,p);}
  if(p!=s.size()) {
    err="unsupported trailing COPY tokens";
    return false;
  }
  return true;
}
inline bool rewrite_copy(const std::string &sql,std::string &rewritten,
                         std::string &err,bool &matched) {
  matched=false;
  size_t p=0;
  if(!copy_word(sql,p,"copy")) return true;
  matched=true;
  copy_skip_ws(sql,p);
  if(p>=sql.size()){err="incomplete COPY statement";return false;}
  const bool source_query=sql[p]=='(';
  std::string source;
  if(source_query) {
    if(!copy_parenthesized_query(sql,p,source,err))return false;
  } else if(!copy_table_name(sql,p,source,err)) return false;
  const bool output=copy_word(sql,p,"to");
  if(!output && !copy_word(sql,p,"from")) {
    err="COPY expects TO or FROM";
    return false;
  }
  if(!output && source_query) {
    err="COPY FROM requires a target table, not a SELECT query";
    return false;
  }
  copy_skip_ws(sql,p);
  std::string path;
  if(!parse_quoted_path(sql,p,path)) {
    err="COPY requires a single-quoted file path";
    return false;
  }
  if(!copy_options(sql,p,err))return false;
  if(output) {
    std::string target;
    if(!copy_output_path(path,target,err))return false;
    const std::string from_expr=source_query?
        "("+source+") AS __seekdb_copy_src":source;
    // INTO OUTFILE must occur before FROM in the seekdb MySQL grammar.
    rewritten="SELECT * INTO OUTFILE "+sql_quote(target)+
        " FIELDS TERMINATED BY ',' OPTIONALLY ENCLOSED BY '\"' "
        "ESCAPED BY '\"' LINES TERMINATED BY '\\n' FROM "+
        from_expr;
  } else {
    std::string source_file;
    if(!resolve_file(path,source_file,err))return false;
    const std::string lp=lower(source_file);
    const bool is_json=lp.size()>=5 && lp.substr(lp.size()-5)==".json";
    const bool is_ndjson=(lp.size()>=7 && lp.substr(lp.size()-7)==".ndjson") ||
                         (lp.size()>=6 && lp.substr(lp.size()-6)==".jsonl");
    const bool is_csv=lp.size()>=4 && lp.substr(lp.size()-4)==".csv";
    if(!is_csv && !is_json && !is_ndjson) {
      err="COPY FROM currently supports .csv, .json and .ndjson/.jsonl only";
      return false;
    }
    rewritten="INSERT INTO "+source+" SELECT * FROM "+
        (is_csv?"read_csv":(is_json?"read_json":"read_ndjson"))+
        "("+sql_quote(source_file)+")";
  }
  return true;
}

inline bool rewrite(const std::string &sql,std::string &out,std::string &err) {
  out.clear();
  if(sql.size()>MAX_RESULT_BYTES){err="SQL exceeds file scan limit";return false;}
  bool is_copy=false;
  std::string copy_sql;
  if(!rewrite_copy(sql,copy_sql,err,is_copy))return false;
  if(is_copy) {
    // COPY FROM introduces a read_csv/read_json table source; pass the
    // transformed SQL back through the existing table-source rewrite.
    return rewrite(copy_sql,out,err);
  }
  if(lower(sql).find("read_csv")==std::string::npos &&
     lower(sql).find("read_json")==std::string::npos &&
     lower(sql).find("read_ndjson")==std::string::npos &&
     lower(sql).find("read_parquet")==std::string::npos &&
     lower(sql).find("glob(")==std::string::npos) {
    out=sql;
    return true;
  }
  bool from=false,expect_table=false;
  int64_t generated_aliases=0;
  enum State { NORMAL, SQUOTE, DQUOTE, BACKTICK, LINE_COMMENT, BLOCK_COMMENT } state=NORMAL;
  for(size_t i=0;i<sql.size();) {
    char c=sql[i];
    if(state==LINE_COMMENT){
      out.push_back(c);++i;
      if(c=='\n')state=NORMAL;
    } else if(state==BLOCK_COMMENT){
      if(c=='*' && i+1<sql.size() && sql[i+1]=='/') {
        out+="*/";i+=2;state=NORMAL;
      }else{out.push_back(c);++i;}
    } else if(state==SQUOTE || state==DQUOTE || state==BACKTICK){
      out.push_back(c);
      char close=state==SQUOTE?'\'':(state==DQUOTE?'"':'`');
      if(c=='\\' && i+1<sql.size()){out.push_back(sql[i+1]);i+=2;}
      else if(c==close && i+1<sql.size() && sql[i+1]==close){
        out.push_back(sql[i+1]);i+=2;
      } else {++i;if(c==close)state=NORMAL;}
    } else if(c=='-' && i+1<sql.size() && sql[i+1]=='-' &&
              (i+2==sql.size() || space(sql[i+2]))) {
      state=LINE_COMMENT;out+="--";i+=2;
    } else if(c=='#') {state=LINE_COMMENT;out.push_back(c);++i;}
    else if(c=='/' && i+1<sql.size() && sql[i+1]=='*'){
      state=BLOCK_COMMENT;out+="/*";i+=2;
    }else if(c=='\''){state=SQUOTE;out.push_back(c);++i;}
    else if(c=='"'){state=DQUOTE;out.push_back(c);++i;}
    else if(c=='`'){state=BACKTICK;out.push_back(c);++i;}
    else if(id_start(c)) {
      size_t end=i+1;
      while(end<sql.size() && id_char(sql[end]))++end;
      std::string token=sql.substr(i,end-i);
      std::string word=lower(token);
      bool table_fn=(word=="read_csv" || word=="read_json" ||
                     word=="read_ndjson" || word=="read_parquet" || word=="glob");
      size_t paren=end;
      while(paren<sql.size() && space(sql[paren]))++paren;
      if(table_fn && expect_table && paren<sql.size() && sql[paren]=='('){
        size_t at=paren+1;
        while(at<sql.size() && space(sql[at]))++at;
        std::string path;
        if(!parse_quoted_path(sql,at,path)){
          err="file table function requires a constant single-quoted path";
          return false;
        }
        while(at<sql.size() && space(sql[at]))++at;
        if(at>=sql.size() || sql[at]!=')'){
          err="only one file path argument is supported";
          return false;
        }
        ++at;
        std::string replacement;
        if(!source_sql(word,path,replacement,err))return false;
        if(out.size()+replacement.size()>MAX_RESULT_BYTES*2){
          err="rewritten SQL exceeds size limit";return false;
        }
        out+=replacement;
        if (file_table_needs_alias(sql, at)) {
          out+=" AS __seekdb_file_"+std::to_string(++generated_aliases);
        }
        expect_table=false;
        i=at;
      }else{
        out+=token;
        i=end;
        if(word=="from" || word=="join"){expect_table=true;from=true;}
        else if(word=="where" || word=="group" || word=="having" ||
                word=="order" || word=="limit" || word=="union") {
          from=false;expect_table=false;
        } else if(expect_table && word!="lateral" && word!="as") expect_table=false;
      }
    }else{
      out.push_back(c);++i;
      if(c==',' && from)expect_table=true;
    }
  }
  return true;
}

} // namespace native_file_sql
} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_PARSER_OB_FILE_SQL_REWRITE_H_
