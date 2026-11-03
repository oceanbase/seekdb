from pathlib import Path
import re
import sys


def type_names(source):
    names = re.findall(r"^\s*(T_[A-Z0-9_]+)[ =0-9]*,", source, flags=re.MULTILINE)
    cases = "".join(f'\tcase {name} : return "{name}";\n' for name in names)
    return (
        '#include "query/engine/expr/ob_item_type.h"\n'
        "const char* get_type_name(int type)\n{\n\tswitch(type){\n"
        + cases
        + '\tdefault:return "Unknown";\n\t}\n}\n'
    )


def main():
    source, output = sys.argv[1:]
    text = Path(source).read_text(encoding="utf-8")
    text = type_names(text)
    with open(output, "w", encoding="utf-8", newline="\n") as stream:
        stream.write(text)


if __name__ == "__main__":
    main()
