#!/usr/bin/env bash

set -euo pipefail

readonly BISON="$(readlink -f "$1")"
readonly FLEX="$(readlink -f "$2")"
readonly BISON_DATA="$(readlink -f "$3")"
readonly OUTPUT_DIR="$([[ "$4" = /* ]] && printf '%s' "$4" || printf '%s/%s' "${PWD}" "$4")"
readonly SOURCE_DIR="$(readlink -f "$5")"
readonly ITEM_TYPE_HEADER="$(readlink -f "$6")"

export BISON_PKGDATADIR="${BISON_DATA}"

if [[ "$("${BISON}" -V | awk 'NR == 1 {print $NF}')" != "2.4.1" ]]; then
  printf 'seekdb SQL parser requires bison 2.4.1\n' >&2
  exit 1
fi

mkdir -p "${OUTPUT_DIR}"

sed_in_place()
{
  if [[ "$(uname -s)" == "Darwin" ]]; then
    sed -i '' "$@"
  else
    sed -i "$@"
  fi
}

run_bison()
{
  local grammar="$1"
  local output="$2"
  local diagnostic

  if ! diagnostic="$("${BISON}" -v -Werror -d "${grammar}" -o "${output}" 2>&1)"; then
    printf '%s\n' "${diagnostic}" >&2
    exit 1
  fi
  if [[ "${diagnostic}" == *conflict* ]]; then
    printf '%s\n' "${diagnostic}" >&2
    exit 1
  fi
}

cp \
  "${SOURCE_DIR}/ftsparser.y" \
  "${OUTPUT_DIR}/ftsparser.bazel.y"
(
  cd "${OUTPUT_DIR}"
  run_bison \
    ftsparser.bazel.y \
    ftsparser_tab.c
)
rm -f "${OUTPUT_DIR}/ftsparser.bazel.y"

cp \
  "${SOURCE_DIR}/ftsblex.l" \
  "${OUTPUT_DIR}/ftsblex.bazel.l"
(
  cd "${OUTPUT_DIR}"
  "${FLEX}" -Cfa -B -8 \
    -o ftsblex_lex.c \
    ftsblex.bazel.l \
    ftsparser_tab.h
)
rm -f "${OUTPUT_DIR}/ftsblex.bazel.l"

sed_in_place '/This var may be unused depending upon options./d' \
  "${OUTPUT_DIR}/ftsblex_lex.c"
sed_in_place \
  -e '/Setup the input buffer state to scan the given bytes/,/}/ {' \
  -e '/int i/d' \
  -e '}' \
  "${OUTPUT_DIR}/ftsblex_lex.c"
sed_in_place \
  -e '/Setup the input buffer state to scan the given bytes/,/}/ {' \
  -e '/for ( i = 0; i < _yybytes_len; ++i )/d' \
  -e '}' \
  "${OUTPUT_DIR}/ftsblex_lex.c"
sed_in_place \
  -e '/Setup the input buffer state to scan the given bytes/,/}/ {' \
  -e 's/\tbuf\[i\] = yybytes\[i\]/memcpy(buf, yybytes, _yybytes_len)/g' \
  -e '}' \
  "${OUTPUT_DIR}/ftsblex_lex.c"

for output in \
  ftsblex_lex.c \
  ftsblex_lex.h \
  ftsparser_tab.c \
  ftsparser_tab.h
do
  sed_in_place \
    -e 's#ftsblex\.bazel\.l#src/sql/parser/ftsblex.l#g' \
    -e 's#ftsparser\.bazel\.y#src/sql/parser/ftsparser.y#g' \
    "${OUTPUT_DIR}/${output}"
done

sed \
  -e 's#"../../../src/sql/parser/sql_parser_mysql_mode_lex.h"#"sql/parser/sql_parser_mysql_mode_lex.h"#' \
  -e 's#"../../../src/sql/parser/sql_parser_base.h"#"sql/parser/sql_parser_base.h"#' \
  "${SOURCE_DIR}/sql_parser_mysql_mode.y" \
  > "${OUTPUT_DIR}/sql_parser_mysql_mode.bazel.y"
# Bison copies the grammar path into every generated #line directive.  Passing
# the sandbox's absolute path here made the parser output both non-reproducible
# and needlessly large.  Invoke it from the output directory so the generated C
# file contains the stable, short action-local name instead.
(
  cd "${OUTPUT_DIR}"
  run_bison \
    sql_parser_mysql_mode.bazel.y \
    sql_parser_mysql_mode_tab.c
)
rm -f "${OUTPUT_DIR}/sql_parser_mysql_mode.bazel.y"

# The legacy lexer hard-codes a source-tree header output. Generate from a
# patched action-local copy so the source tree remains immutable.
sed \
  's#%option header-file=.*#%option header-file="sql_parser_mysql_mode_lex.h"#' \
  "${SOURCE_DIR}/sql_parser_mysql_mode.l" \
  > "${OUTPUT_DIR}/sql_parser_mysql_mode.bazel.l"
(
  cd "${OUTPUT_DIR}"
  "${FLEX}" -Cfa -B -8 \
    -o sql_parser_mysql_mode_lex.c \
    sql_parser_mysql_mode.bazel.l \
    sql_parser_mysql_mode_tab.h
)
rm -f "${OUTPUT_DIR}/sql_parser_mysql_mode.bazel.l"

sed_in_place \
  -e '/Setup the input buffer state to scan the given bytes/,/}/ {' \
  -e '/int i/d' \
  -e '}' \
  "${OUTPUT_DIR}/sql_parser_mysql_mode_lex.c"
sed_in_place \
  -e '/Setup the input buffer state to scan the given bytes/,/}/ {' \
  -e '/for ( i = 0; i < _yybytes_len; ++i )/d' \
  -e '}' \
  "${OUTPUT_DIR}/sql_parser_mysql_mode_lex.c"
sed_in_place \
  -e '/Setup the input buffer state to scan the given bytes/,/}/ {' \
  -e 's/\tbuf\[i\] = yybytes\[i\]/memcpy(buf, yybytes, _yybytes_len)/g' \
  -e '}' \
  "${OUTPUT_DIR}/sql_parser_mysql_mode_lex.c"
sed_in_place \
  -e '/obsql_mysql_yylex_init is special because it creates the scanner itself/,/Initialization is the same as for the non-reentrant scanner/ {' \
  -e 's/return 1/return errno/g' \
  -e '}' \
  "${OUTPUT_DIR}/sql_parser_mysql_mode_lex.c"

"${SOURCE_DIR}/gen_type_name.sh" "${ITEM_TYPE_HEADER}" \
  > "${OUTPUT_DIR}/type_name.c"
