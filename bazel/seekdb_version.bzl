"""Generates seekdb's version source from Bazel workspace status."""

_GIT_REVISION_KEY = "STABLE_SEEKDB_GIT_REVISION"
_GIT_BRANCH_KEY = "STABLE_SEEKDB_GIT_BRANCH"

def _seekdb_version_source_impl(ctx):
    output = ctx.outputs.out

    ctx.actions.run_shell(
        inputs = [
            ctx.file.template,
            ctx.info_file,
        ],
        outputs = [output],
        arguments = [
            ctx.file.template.path,
            ctx.info_file.path,
            output.path,
        ],
        command = """
set -eu

template="$1"
status_file="$2"
output="$3"

read_status() {
  key="$1"
  value="$(awk -v key="${key}" '
    $1 == key {
      $1 = ""
      sub(/^ /, "")
      print
      found = 1
      exit
    }
    END {
      if (!found) {
        exit 1
      }
    }
  ' "${status_file}")" || {
    echo "missing workspace status key: ${key}" >&2
    exit 1
  }
  printf '%%s' "${value}"
}

escape_sed_replacement() {
  printf '%%s' "$1" | sed 's/[\\\\&|]/\\\\&/g'
}

git_revision="$(escape_sed_replacement "$(read_status %s)")"
git_branch="$(escape_sed_replacement "$(read_status %s)")"

mkdir -p "$(dirname "${output}")"
sed \
  -e 's|@BUILD_NUMBER@|1|g' \
  -e "s|@GIT_REVISION@|${git_revision}|g" \
  -e "s|@GIT_BRANCH@|${git_branch}|g" \
  -e 's|@BUILD_FLAGS@|RelWithDebInfo|g' \
  -e 's|@BUILD_INFO@||g' \
  "${template}" > "${output}"
""" % (
            _GIT_REVISION_KEY,
            _GIT_BRANCH_KEY,
        ),
        mnemonic = "SeekdbVersionSource",
        progress_message = "Generating %{output}",
    )

    return [DefaultInfo(files = depset([output]))]

_seekdb_version_source = rule(
    implementation = _seekdb_version_source_impl,
    attrs = {
        "out": attr.output(mandatory = True),
        "template": attr.label(
            allow_single_file = True,
            mandatory = True,
        ),
    },
)

def seekdb_version_source(
        name,
        template,
        out,
        tags = [],
        visibility = None):
    """Expands ob_version.cpp.in into an action-local generated source."""

    _seekdb_version_source(
        name = name,
        template = template,
        out = out,
        tags = tags,
        visibility = visibility,
    )
