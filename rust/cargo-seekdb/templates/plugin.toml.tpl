manifest_schema_version = 1
plugin_id = "@@PLUGIN_ID@@"
package_version = "1.0.0"
build_id = "@@NAME@@-v1"
abi_major = 1
abi_minor = 0
catalog_schema_version = 1
data_format_version = 0
entrypoint = "@@LIBRARY@@"
capabilities = ["extension.catalog.v1", "extension.registration.v1"]

[[provides]]
service_id = "@@PLUGIN_ID@@.chars"
version = "1.0.0"
