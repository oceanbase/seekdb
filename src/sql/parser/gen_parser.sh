#!/bin/bash
#
# AUTHOR: Zhifeng YANG
# DATE: 2012-10-24
# DESCRIPTION:
#
set +x
CURDIR="$(dirname $(readlink -f "$0"))"
#export PATH=/usr/local/bin:$PATH
export PATH=$CURDIR/../../../deps/3rd/usr/local/oceanbase/devtools/bin/:$PATH
export BISON_PKGDATADIR=$CURDIR/../../../deps/3rd/usr/local/oceanbase/devtools/share/bison
CACHE_MD5_FILE=$CURDIR/_MD5
TEMP_FILE=$(mktemp)

# Detect OS and set sed inplace option for compatibility between Linux and Mac
# Mac requires sed -i '' while Linux requires sed -i
# Use a function to avoid quote expansion issues
if [[ "$OSTYPE" == "darwin"* ]]; then
    sed_inplace() {
        sed -i '' "$@"
    }
else
    sed_inplace() {
        sed -i "$@"
    }
fi

BISON_VERSION=`bison -V| grep 'bison (GNU Bison)'|awk '{ print  $4;}'`
NEED_VERSION='2.4.1'

if [ "$BISON_VERSION" != "$NEED_VERSION" ]; then
  echo "bison version not match, please use bison-$NEED_VERSION"
  exit 1
fi

cat ../../../src/sql/parser/sql_parser_mysql_mode.y >> $TEMP_FILE
cat ../../../src/sql/parser/sql_parser_mysql_mode.l >> $TEMP_FILE
cat ../../../deps/oblib/src/common/ob_item_type.h >> $TEMP_FILE

md5sum_value=$(md5sum "$TEMP_FILE" | awk '{ print $1 }')

# Check if any required output files are missing.
outputs_missing() {
  local required_files=(
    "$CURDIR/ftsparser_tab.c"
    "$CURDIR/ftsparser_tab.h"
    "$CURDIR/ftsblex_lex.c"
    "$CURDIR/sql_parser_mysql_mode_tab.c"
    "$CURDIR/sql_parser_mysql_mode_tab.h"
    "$CURDIR/sql_parser_mysql_mode_lex.c"
    "$CURDIR/type_name.c"
  )
  for file in "${required_files[@]}"; do
    if [[ ! -s "$file" ]]; then
      return 0
    fi
  done
  return 1
}

bison_parser() {
BISON_OUTPUT="$(bison -v -Werror -d $1 -o $2 2>&1)"
BISON_RETURN="$?"
echo $BISON_OUTPUT
if [ $BISON_RETURN -ne 0 ]
  then
  >&2 echo "Compile error: $BISON_OUTPUT, abort."
  exit 1
fi
if [[ $BISON_OUTPUT == *"conflict"* ]]
then
  >&2 echo "Compile conflict: $BISON_OUTPUT, abort."
  exit 1
fi
}

function generate_parser {

# fts boolean mode parser for mysql
bison_parser ../../../src/sql/parser/ftsparser.y ../../../src/sql/parser/ftsparser_tab.c
flex -Cfa -B -8 -o ../../../src/sql/parser/ftsblex_lex.c ../../../src/sql/parser/ftsblex.l ../../../src/sql/parser/ftsparser_tab.h

sed_inplace '/This var may be unused depending upon options./d' ../../../src/sql/parser/ftsblex_lex.c
sed_inplace "/Setup the input buffer state to scan the given bytes/,/}/{/int i/d}" ../../../src/sql/parser/ftsblex_lex.c
sed_inplace "/Setup the input buffer state to scan the given bytes/,/}/{/for ( i = 0; i < _yybytes_len; ++i )/d}" ../../../src/sql/parser/ftsblex_lex.c
sed_inplace "/Setup the input buffer state to scan the given bytes/,/}/{s/\tbuf\[i\] = yybytes\[i\]/memcpy(buf, yybytes, _yybytes_len)/g}" ../../../src/sql/parser/ftsblex_lex.c

# generate mysql sql_parser
bison_parser ../../../src/sql/parser/sql_parser_mysql_mode.y ../../../src/sql/parser/sql_parser_mysql_mode_tab.c
flex -Cfa -B -8 -o ../../../src/sql/parser/sql_parser_mysql_mode_lex.c ../../../src/sql/parser/sql_parser_mysql_mode.l ../../../src/sql/parser/sql_parser_mysql_mode_tab.h

sed_inplace "/Setup the input buffer state to scan the given bytes/,/}/{/int i/d}" ../../../src/sql/parser/sql_parser_mysql_mode_lex.c
sed_inplace "/Setup the input buffer state to scan the given bytes/,/}/{/for ( i = 0; i < _yybytes_len; ++i )/d}" ../../../src/sql/parser/sql_parser_mysql_mode_lex.c
sed_inplace "/Setup the input buffer state to scan the given bytes/,/}/{s/\tbuf\[i\] = yybytes\[i\]/memcpy(buf, yybytes, _yybytes_len)/g}" ../../../src/sql/parser/sql_parser_mysql_mode_lex.c
sed_inplace "/obsql_mysql_yylex_init is special because it creates the scanner itself/,/Initialization is the same as for the non-reentrant scanner/{s/return 1/return errno/g}" ../../../src/sql/parser/sql_parser_mysql_mode_lex.c


# generate type name
./gen_type_name.sh ../../../deps/oblib/src/common/ob_item_type.h > type_name.c

echo "$md5sum_value" > $CACHE_MD5_FILE
}

if [[ -n "$NEED_PARSER_CACHE" && "$NEED_PARSER_CACHE" == "ON" ]]; then
    echo "generate sql parser with cache"
    origin_md5sum_value=$(<$CACHE_MD5_FILE)
    if [[ "$md5sum_value" == "$origin_md5sum_value" ]] && ! outputs_missing; then 
      echo "hit the md5 cache"
    else
      generate_parser
    fi
else
    echo "generate sql parser without cache"
    generate_parser
fi

rm -rf $TEMP_FILE
