#!/bin/bash

set -euo pipefail

if [[ $# -ne 1 || ! -f "$1" ]]; then
  echo "usage: $0 <canonical-ob-item-type-header>" >&2
  exit 1
fi

if ! grep -Eq '^[[:space:]]*T_[A-Z0-9_]+' "$1"; then
  echo "no ObItemType enumerators found in $1" >&2
  exit 1
fi
echo -e '#include "sql/parser/ob_item_type.h"'
echo -e "const char* get_type_name(int type)\n{"
echo -e "\tswitch(type){"
sed -rn 's/\s*(T_[A-Z0-9_]+)[ =0-9]*,/\tcase \1 : return \"\1\";/p' "$1"
echo -e '\tdefault:return "Unknown";\n\t}\n}'
