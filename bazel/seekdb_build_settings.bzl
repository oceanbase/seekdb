"""Command-line build settings owned by seekdb."""

def _bool_flag_impl(ctx):
    _ = ctx.build_setting_value
    return []

seekdb_bool_flag = rule(
    implementation = _bool_flag_impl,
    build_setting = config.bool(flag = True),
)
