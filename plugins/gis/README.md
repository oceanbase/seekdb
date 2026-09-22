# seekdb GIS plugin

This package publishes the GIS SQL surface through the execution SPI and returns
host-owned geometry payloads. It provides `POINT`, `ST_MakeEnvelope` and
2D/3D `ST_MakePoint`, plus byte-oriented `ST_X`, `ST_Y`,
`ST_SRID`, `ST_AsWKB`, `ST_AsBinary`, `_ST_GeometryType`, `ST_IsValid`,
`ST_AsText`, `ST_AsWKT`, `ST_GeomFromWKB` and `ST_GeometryFromWKB`
accessors, plus `_ST_SetSRID` geometry metadata mutation, `ST_Area`, `ST_Length`,
`ST_Distance`, WKT/GeoJSON constructors, collection constructors, spatial
indexes, relations, transforms, topology/buffer/MVT operations, GeoHash and a
stable Morton spatial-cell key. The extension catalog currently publishes 65
function descriptors.
`POINT` takes two non-null little-endian
IEEE-754 `double` values (`x`, `y`); `ST_MakeEnvelope` takes four
(`xmin`, `ymin`, `xmax`, `ymax`) with default SRID 0. `ST_MakePoint` accepts the
same 2D arguments or an additional `z`. The result type is
`org.seekdb.gis.geometry`; Point and PointZ payloads are 26 and 34 bytes, while
the envelope polygon payload is 98 bytes, all using seekdb's
SRID/version/byte-order/WKB wire format.

The package intentionally contains no seekdb private headers or libraries. The
core geometry adapters now map `ObDatum` to this byte-oriented ABI when the
service is active, while retaining the compatibility path for an uninstalled or
inactive plugin. The plugin owns recursive WKB decoding/encoding, WKT and
GeoJSON codecs, metrics, centroid/MBR, relation predicates, point buffering,
tile-coordinate transformation/clipping, rectangle clipping and boolean
geometry composition. The implementation is deliberately dependency-free; an
optional higher-precision algorithm pack can replace these services without
changing SQL registration or the execution ABI.

Build with:

```bash
cmake -S . -B build_release -DSEEKDB_ENABLE_EXPERIMENTAL_PLUGINS=ON
ob-make -C build_release seekdb_gis_plugin
```

To run the lightweight profile, keep core GIS disabled and place the package
under the server base directory.  The first server start discovers
`plugin.toml`, records the package in seekdb's SQL system catalog, and loads the shared object
before the server becomes ready:

```bash
BASE=/data/seekdb
mkdir -p "$BASE/plugins/gis"
cp build_release/plugins/gis/seekdb_gis.so "$BASE/plugins/gis/"
cp plugins/gis/plugin.toml "$BASE/plugins/gis/"
build_release/src/observer/seekdb --base-dir="$BASE"
```

For the standard local test directory, the repository also provides a
deployment helper. It stops only the seekdb process recorded by that base
directory, installs the rebuilt core and package, starts seekdb, and prints the
durable plugin catalog:

```bash
tools/seekdb_gis_plugin_deploy.sh
```

Use `--base-dir` and `--build-dir` when the data or build directories differ
from their defaults.

The SQL constructor exposed by the current MySQL grammar is `POINT`; for
example:

```sql
SELECT ST_X(POINT(1, 2)), ST_Y(POINT(1, 2));
```

Plugin discovery and loading happen during startup, so restart seekdb after
replacing a package.  Phase 1 uses local identity pinning only; signatures
and content-hash trust are intentionally deferred.
