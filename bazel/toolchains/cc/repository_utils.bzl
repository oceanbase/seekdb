"""Compiler discovery shared by the repository-owned toolchains."""

def host_substitutions(ctx):
    return {
        "@HOST_OS@": {"linux": "linux", "mac os x": "macos"}[ctx.os.name],
        "@HOST_CPU@": {"amd64": "x86_64", "x86_64": "x86_64", "aarch64": "aarch64", "arm64": "aarch64"}[ctx.os.arch],
    }

def builtin_include_directories(ctx, compiler):
    result = ctx.execute(
        [compiler, "-E", "-x", "c++", "/dev/null", "-v"],
        environment = {"LC_ALL": "C"},
    )
    if result.return_code:
        fail("Cannot query %s include directories: %s" % (compiler, result.stderr))
    includes = []
    collecting = False
    for line in result.stderr.splitlines():
        value = line.strip()
        if value == "#include <...> search starts here:":
            collecting = True
        elif value == "End of search list.":
            break
        elif collecting:
            includes.append(value.removesuffix(" (framework directory)"))
    if not includes:
        fail("%s did not report builtin include directories" % compiler)
    return includes
