#!/usr/bin/env bash

set -euo pipefail

readonly BISON="$(readlink -f "$1")"
readonly FLEX="$(readlink -f "$2")"
readonly BISON_DATA="$(readlink -f "$3")"
readonly OUTPUT_DIR="$([[ "$4" = /* ]] && printf '%s' "$4" || printf '%s/%s' "${PWD}" "$4")"
readonly SOURCE_DIR="$(readlink -f "$5")"

export BISON_PKGDATADIR="${BISON_DATA}"

if [[ "$("${BISON}" -V | awk 'NR == 1 {print $NF}')" != "2.4.1" ]]; then
  printf 'seekdb PL parser requires bison 2.4.1\n' >&2
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

cp \
  "${SOURCE_DIR}/pl_parser_mysql_mode.y" \
  "${OUTPUT_DIR}/pl_parser_mysql_mode.bazel.y"
diagnostic="$(
  cd "${OUTPUT_DIR}"
  "${BISON}" -v -Werror -d \
    pl_parser_mysql_mode.bazel.y \
    -o pl_parser_mysql_mode_tab.c \
    2>&1
)" || {
  printf '%s\n' "${diagnostic}" >&2
  exit 1
}
if [[ "${diagnostic}" == *conflict* ]]; then
  printf '%s\n' "${diagnostic}" >&2
  exit 1
fi
rm -f "${OUTPUT_DIR}/pl_parser_mysql_mode.bazel.y"

# The legacy lexer writes its header back into src/pl/parser. Generate from an
# action-local copy with a local header path so Bazel never mutates the source
# tree or consumes ignored generated files.
sed \
  's#%option header-file=.*#%option header-file="pl_parser_mysql_mode_lex.h"#' \
  "${SOURCE_DIR}/pl_parser_mysql_mode.l" \
  > "${OUTPUT_DIR}/pl_parser_mysql_mode.bazel.l"
(
  cd "${OUTPUT_DIR}"
  "${FLEX}" \
    -o pl_parser_mysql_mode_lex.c \
    pl_parser_mysql_mode.bazel.l \
    pl_parser_mysql_mode_tab.h
)
rm -f "${OUTPUT_DIR}/pl_parser_mysql_mode.bazel.l"

for output in \
  pl_parser_mysql_mode_lex.c \
  pl_parser_mysql_mode_lex.h \
  pl_parser_mysql_mode_tab.c \
  pl_parser_mysql_mode_tab.h
do
  sed_in_place \
    -e 's#pl_parser_mysql_mode\.bazel\.l#src/pl/parser/pl_parser_mysql_mode.l#g' \
    -e 's#pl_parser_mysql_mode\.bazel\.y#src/pl/parser/pl_parser_mysql_mode.y#g' \
    "${OUTPUT_DIR}/${output}"
done
