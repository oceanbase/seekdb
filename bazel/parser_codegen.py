from pathlib import Path
import re
import sys


def patch_lexer(source, kind):
    source, count = re.subn(
        r"for \( i = 0; i < _yybytes_len; \+\+i \)\s*buf\[i\] = yybytes\[i\];",
        "memcpy(buf, yybytes, _yybytes_len);",
        source,
    )
    if count != 1:
        raise ValueError("Expected one Flex scan-buffer copy loop")
    source = re.sub(
        r"(Setup the input buffer state to scan the given bytes.*?){(.*?)}",
        lambda match: match[1] + "{" + re.sub(r"\s*int i;", "", match[2]) + "}",
        source,
        flags=re.DOTALL,
    )
    if kind == "sql":
        start = source.index("yylex_init is special because it creates the scanner itself")
        end = source.index("Initialization is the same as for the non-reentrant scanner", start)
        source = source[:start] + source[start:end].replace("return 1;", "return errno;") + source[end:]
    return source


def type_names(source):
    names = re.findall(
        r"^\s*(T_[A-Z0-9_]+)(?:\s*=\s*-?[0-9]+)?\s*,", source, flags=re.MULTILINE
    )
    cases = "".join(f'\tcase {name} : return "{name}";\n' for name in names)
    return (
        '#include "query/parser/ob_item_type.h"\n'
        "const char* get_type_name(int type)\n{\n\tswitch(type){\n"
        + cases
        + '\tdefault:return "Unknown";\n\t}\n}\n'
    )


def main():
    kind, source, output = sys.argv[1:]
    text = Path(source).read_text(encoding="utf-8")
    text = type_names(text) if kind == "type_names" else patch_lexer(text, kind)
    with open(output, "w", encoding="utf-8", newline="\n") as stream:
        stream.write(text)


if __name__ == "__main__":
    main()
