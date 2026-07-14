#define USING_LOG_PREFIX SQL_ENG

#include "ob_expr_load_file.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string>

#include "lib/utility/utility.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_location_schema_struct.h"

using namespace oceanbase::common;
using namespace oceanbase::share::schema;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{

namespace
{

static const char *FILE_URL_PREFIX = "file://";
static const int64_t FILE_URL_PREFIX_LEN = 7;

bool starts_with_file_url(const ObString &url)
{
  return url.length() >= FILE_URL_PREFIX_LEN
      && 0 == MEMCMP(url.ptr(), FILE_URL_PREFIX, FILE_URL_PREFIX_LEN);
}

bool is_invalid_file_name(const ObString &file_name)
{
  if (file_name.empty()) {
    return true;
  }

  std::string name(file_name.ptr(), file_name.length());

  if (!name.empty() && name[0] == '/') {
    return true;
  }

  if (name.find("..") != std::string::npos) {
    return true;
  }

  return false;
}

int build_local_file_path(const ObString &location_url,
                          const ObString &file_name,
                          std::string &file_path)
{
  int ret = OB_SUCCESS;

  if (!starts_with_file_url(location_url)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("only file:// location url is supported",
             K(ret), K(location_url));
  } else if (is_invalid_file_name(file_name)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid file name", K(ret), K(file_name));
  } else {
    std::string dir(location_url.ptr() + FILE_URL_PREFIX_LEN,
                    location_url.length() - FILE_URL_PREFIX_LEN);
    std::string name(file_name.ptr(), file_name.length());

    file_path = dir;

    if (!file_path.empty() && file_path[file_path.length() - 1] != '/') {
      file_path += "/";
    }

    file_path += name;
  }

  return ret;
}

int read_whole_file_to_datum(const ObExpr &expr,
                             ObEvalCtx &ctx,
                             const std::string &file_path,
                             ObDatum &res)
{
  int ret = OB_SUCCESS;
  int fd = -1;
  struct stat st;

  if ((fd = ::open(file_path.c_str(), O_RDONLY)) < 0) {
    ret = OB_FILE_NOT_EXIST;
    LOG_WARN("failed to open file", K(ret), K(file_path), KERRMSG);
  } else if (0 != ::fstat(fd, &st)) {
    ret = OB_IO_ERROR;
    LOG_WARN("failed to stat file", K(ret), K(file_path), KERRMSG);
  } else if (st.st_size < 0) {
    ret = OB_IO_ERROR;
    LOG_WARN("invalid file size", K(ret), K(file_path), K(st.st_size));
  } else {
    const int64_t file_size = st.st_size;
    char *buf = expr.get_str_res_mem(ctx, file_size);

    if (OB_ISNULL(buf) && file_size > 0) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate result buffer", K(ret), K(file_size));
    } else {
      int64_t total_read = 0;

      while (OB_SUCC(ret) && total_read < file_size) {
        const ssize_t read_size =
            ::read(fd, buf + total_read, file_size - total_read);

        if (read_size > 0) {
          total_read += read_size;
        } else if (read_size == 0) {
          ret = OB_IO_ERROR;
          LOG_WARN("unexpected end of file", K(ret), K(file_path),
                   K(total_read), K(file_size));
        } else if (errno != EINTR) {
          ret = OB_IO_ERROR;
          LOG_WARN("failed to read file", K(ret), K(file_path), KERRMSG);
        }
      }

      if (OB_SUCC(ret)) {
        res.set_string(buf, total_read);
      }
    }
  }

  if (fd >= 0) {
    ::close(fd);
  }

  return ret;
}

} // namespace

ObExprLoadFile::ObExprLoadFile(common::ObIAllocator &alloc)
    : ObFuncExprOperator(alloc,
                         T_FUN_SYS_LOAD_FILE,
                         N_LOAD_FILE,
                         2,
                         NOT_VALID_FOR_GENERATED_COL,
                         NOT_ROW_DIMENSION)
{
}

ObExprLoadFile::~ObExprLoadFile()
{
}

int ObExprLoadFile::calc_result_typeN(ObExprResType &type,
                                      ObExprResType *types_stack,
                                      int64_t param_num,
                                      common::ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);

  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(param_num != 2)) {
    ret = OB_ERR_PARAM_SIZE;
    LOG_WARN("load_file requires two arguments", K(ret), K(param_num));
  } else if (!ob_is_string_tc(types_stack[0].get_type())
             || !ob_is_string_tc(types_stack[1].get_type())) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_WARN("invalid argument type",
             K(ret),
             K(types_stack[0].get_type()),
             K(types_stack[1].get_type()));
  } else {
    types_stack[0].set_calc_type(ObVarcharType);
    types_stack[0].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);

    types_stack[1].set_calc_type(ObVarcharType);
    types_stack[1].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);

    type.set_varchar();
    type.set_collation_type(CS_TYPE_BINARY);
    type.set_calc_collation_type(CS_TYPE_BINARY);
    type.set_length(OB_MAX_LONGTEXT_LENGTH);
  }

  return ret;
}

int ObExprLoadFile::eval_load_file(const ObExpr &expr,
                                   ObEvalCtx &ctx,
                                   ObDatum &res)
{
  int ret = OB_SUCCESS;

  ObDatum *location_datum = NULL;
  ObDatum *file_datum = NULL;

  if (OB_UNLIKELY(expr.arg_cnt_ != 2)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected argument count", K(ret), K(expr.arg_cnt_));
  } else if (OB_FAIL(expr.args_[0]->eval(ctx, location_datum))) {
    LOG_WARN("failed to eval location name", K(ret));
  } else if (OB_FAIL(expr.args_[1]->eval(ctx, file_datum))) {
    LOG_WARN("failed to eval file name", K(ret));
  } else if (location_datum->is_null() || file_datum->is_null()) {
    res.set_null();
  } else {
    const ObString location_name = location_datum->get_string();
    const ObString file_name = file_datum->get_string();

    ObSchemaGetterGuard *schema_guard = NULL;
    const ObLocationSchema *location_schema = NULL;

    if (OB_ISNULL(ctx.exec_ctx_.get_sql_ctx())
        || OB_ISNULL(ctx.exec_ctx_.get_sql_ctx()->schema_guard_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("schema guard is null", K(ret));
    } else if (FALSE_IT(schema_guard = ctx.exec_ctx_.get_sql_ctx()->schema_guard_)) {
    } else if (OB_FAIL(schema_guard->get_location_schema_by_name(location_name,
                                                                 location_schema))) {
      LOG_WARN("failed to get location schema by name",
               K(ret), K(location_name));
    } else if (OB_ISNULL(location_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("location schema is null", K(ret), K(location_name));
    } else {
      const ObString &location_url = location_schema->get_location_url_str();
      std::string file_path;

      if (OB_FAIL(build_local_file_path(location_url, file_name, file_path))) {
        LOG_WARN("failed to build local file path",
                 K(ret), K(location_url), K(file_name));
      } else if (OB_FAIL(read_whole_file_to_datum(expr, ctx, file_path, res))) {
        LOG_WARN("failed to read file", K(ret), K(file_path));
      }
    }
  }

  return ret;
}

int ObExprLoadFile::cg_expr(ObExprCGCtx &expr_cg_ctx,
                            const ObRawExpr &raw_expr,
                            ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);

  int ret = OB_SUCCESS;
  rt_expr.eval_func_ = ObExprLoadFile::eval_load_file;

  return ret;
}

} // namespace sql
} // namespace oceanbase