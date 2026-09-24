# seekdb GIS plugin

**Experimental and not a completed semantic migration.** Several topology and
distance operations below still use bounding boxes or vertices in place of
the full geometry. The diagnostic regression currently fails four basic cases:
different crossing lines compare equal, disjoint parallel lines intersect,
point-to-segment distance is measured to an endpoint, and disjoint unit squares
produce union area 3 instead of 2. SQL package/binding tests passing does not
make these algorithms equivalent to the existing SeekDB GIS or PostGIS.
Do not use this implementation for correctness-sensitive GIS processing.

This SQL extension binds database-owned routines to the GIS execution SPI and returns
host-owned geometry payloads. It provides `POINT`, `ST_MakeEnvelope` and
2D/3D `ST_MakePoint`, plus byte-oriented `ST_X`, `ST_Y`,
`ST_SRID`, `ST_AsWKB`, `ST_AsBinary`, `_ST_GeometryType`, `ST_IsValid`,
`ST_AsText`, `ST_AsWKT`, `ST_GeomFromWKB` and `ST_GeometryFromWKB`
accessors, plus `_ST_SetSRID` geometry metadata mutation, `ST_Area`, `ST_Length`,
`ST_Distance`, WKT/GeoJSON constructors, collection constructors, spatial
indexes, relations, transforms, topology/buffer/MVT operations, GeoHash and a
stable Morton spatial-cell key. The native module registers implementation-only
function descriptors and supporting type/cast objects; the SQL package owns the
public function names, aliases and overloads in each database.
At the algorithm boundary, `POINT` takes two non-null little-endian
IEEE-754 `double` values (`x`, `y`); `ST_MakeEnvelope` takes four
(`xmin`, `ymin`, `xmax`, `ymax`) with default SRID 0. `ST_MakePoint` accepts the
same 2D arguments or an additional `z`. The result type is
`org.seekdb.gis.geometry`; Point and PointZ payloads are 26 and 34 bytes, while
the envelope polygon payload is 98 bytes, all using seekdb's
SRID/version/byte-order/WKB wire format.

The package intentionally contains no seekdb private headers or libraries. With
core GIS disabled, ordinary GIS SQL names resolve through database routine
schemas and the generic native `ObExprUDF` bridge to `PluginFunctionExpr`.
The SQL declarations specify types, overloads and compatibility aliases
(`POINT`, `AREA`, `CENTROID`, collection constructors and underscore-prefixed
names); the module checks exact ABI types/arity and implements NULL propagation
and numeric/SRID conversion.
The generic host adapter evaluates arguments, materializes LOBs, binds the
catalog identity and returns the result in the appropriate SQL datum format.
There is no plugin-mode `ObExprSTArea` implementation. Other legacy numeric
expression entries remain for internal spatial plan construction; `spatial_cellid`
and `spatial_mbr` retain their internal SQL adapters. A missing/inactive plugin
does not fall back to a built-in implementation of the public GIS SQL names.

To add an ordinary GIS function, add its implementation service and typed
descriptor and SQL declaration to this package; do not add an expression class, name registration
or function-specific result sink to the kernel. Core-GIS-enabled builds keep
their original built-in implementations.

SQL decimal/decimal-integer arguments are transported as exact decimal text and
converted by the plugin, not read as floating-point bit patterns. WKT and WKB
results use the generic `core.type.text` and `core.type.blob` representations to
retain long-value capacity and text/binary SQL collations. The host wraps and
unwraps their LOB representation at the common call boundary.

This migration covers ordinary scalar SQL calls, not new spatial optimizer or
index integration. Public calls now follow the generic native-routine path's
generated-column restrictions; enabling them in generated columns requires
durable function-dependency tracking, not merely marking a callback immutable.

The plugin owns recursive WKB decoding/encoding, WKT and
GeoJSON codecs, metrics, centroid/MBR, relation predicates, point buffering,
tile-coordinate transformation/clipping, rectangle clipping and boolean
geometry composition. The implementation is deliberately dependency-free; an
optional higher-precision algorithm pack can replace these services without
changing SQL registration or the execution ABI.

`ST_Transform` currently implements the EPSG:4326 ↔ EPSG:3857 pair (x is
longitude/easting, y is latitude/northing) and identity transforms, not a general
EPSG/PROJ registry. It uses the
[Web Mercator equations](https://proj.org/en/stable/operations/projections/webmerc.html)
with radius 6378137 m. It preserves Z, recursively transforms points/rings and
collection children, and does not clamp latitude to the web-tile cutoff.
Forward latitude must be strictly between -90 and 90 degrees; non-finite
results and unsupported non-identity SRID pairs return an error without emitting
a geometry. Unsupported pairs must not silently relabel unchanged coordinates.
System math routines remain private plugin dependencies, not kernel GIS calls.

The direct-DSO regression includes independent forward and inverse numerical
controls, hemispheres, high latitudes, PointZ, overflow and rejection cases.
For (2°, 49°), the old approximation produced northing about 5279943.671 m
instead of 6274861.394 m. This failure was reproduced before replacing the
approximation. Passing these controls is not proof of arbitrary CRS support.

Build with:

```bash
cmake -S . -B build_release -DSEEKDB_ENABLE_EXPERIMENTAL_PLUGINS=ON
cmake --build build_release --target seekdb_gis_plugin -j8
```

Reproduce the known algorithm gaps independently of the server:

```bash
g++ -std=c++17 -Iinclude rust/plugin-runtime/tests/gis_topology_probe.cpp \
  -o /tmp/seekdb-gis-topology-probe
/tmp/seekdb-gis-topology-probe
```

This compiles the actual private algorithm implementation into a diagnostic
translation unit. It deliberately exits 1 while any expected result differs;
it is not a passing characterization test of the approximations and is not
part of the currently passing CTest subset. It does not exercise DSO loading,
SQL, transactions or storage. Complete replacement/extraction of the geometry
backend and a broader conformance suite are required, not just more smoke tests.

To run the lightweight profile, keep core GIS disabled and place the package
under the server base directory.  The first server start discovers
`plugin.toml`, records the package in seekdb's SQL system catalog, and loads the shared object
before the server becomes ready. Loading the DSO no longer publishes public GIS
SQL function names. Also deliver the control/SQL package and select its root:

```bash
BASE=/data/seekdb
mkdir -p "$BASE/plugins/gis"
mkdir -p "$BASE/share/seekdb/extension"
cp build_release/plugins/gis/seekdb_gis.so "$BASE/plugins/gis/"
cp plugins/gis/plugin.toml "$BASE/plugins/gis/"
cp plugins/gis/sql/gis.control plugins/gis/sql/gis--1.0.sql "$BASE/share/seekdb/extension/"
build_release/src/observer/seekdb --base-dir="$BASE" --extension-dir="$BASE/share/seekdb/extension"
```

Experimental server validation sequence (not yet a verified live-server result;
use a disposable instance and an administrative account):

```sql
CREATE DATABASE gis_demo;
USE gis_demo;
CREATE EXTENSION gis;
SELECT ST_X(POINT(1, 2)), ST_Y(POINT(1, 2));
```

Plugin discovery and loading happen during startup, so restart seekdb after
replacing a package.  Phase 1 uses local identity pinning only; signatures
and content-hash trust are intentionally deferred.

## SQL/LOB regression

After completing a plugin-enabled, core-GIS-disabled production build, run:

```bash
python3 rust/plugin-runtime/tests/gis_sql.py --build-dir build_plugin_overlay_verify
```

This builds/audits the GIS DSO and links a private test executable using the
production kernel objects. It exercises real SQL parsing, type inference,
codegen, LOB materialization and plugin callbacks for distance, area, length,
WKT/WKB round trips, SRID, predicates, collections and NULLs. It also checks
catalog-only name resolution, generic bound calls (no per-GIS-function dispatch),
integer/decimal/string coercions, aliases, invalid arity and invalid SRIDs. Separate payload
checks cover raw/in-row geometry and text, embedded NUL and copied ownership.
Geometry collections retain child type names when serialized to WKT.
The batch case evaluates `ST_Area` on native geometry column datums with LOB
headers, NULLs, skipped rows, cached evaluation and more than 1024 input rows.

The host must materialize SQL LOB datums before passing bytes to the plugin;
an internal LOB header/locator is not part of the GIS payload ABI. Temporary
input buffers and scalar controls must survive through result emission.

The test does not start or change a server. Activation catalog metadata and
LOB storage services are controlled fixtures; it does not verify out-of-row
storage, spatial indexes, recovery or all GIS semantics. After deploying both
the rebuilt host and GIS DSO, rerun SQL smoke tests on the real server.

### Opt-in disposable-server acceptance runner

With core GIS disabled, the implementation-only GIS module loaded, the SQL
package delivered under `--extension-dir`, and PyMySQL installed:

```bash
python3 rust/plugin-runtime/tests/gis_extension_server.py \
  --port 2881 --user root --confirm-disposable-server
```

Use only a disposable instance. This command creates two uniquely named
databases and one user; it does not deploy modules, restart the server or change
global settings. An administrative password may be supplied through
`SEEKDB_TEST_PASSWORD`, not a command-line argument. Alternatively specify an
absolute `--unix-socket` instead of `--port`; no TCP fallback is performed.

The runner checks all 106 routine bindings, fresh overload slots and membership,
module dependencies, cross-session/two-database visibility, qualified calls,
GIS values/long LOBs, per-overload EXECUTE and prepared-call revocation. It also
checks DROP EXTENSION, fresh identities after reinstall, removal of live object
ACL entries, independence of the other database, and database-drop cleanup.
It does not invent a GIS 1.1 update: version updates, recovery, concurrent DDL
and injected commit failures remain separate gates.

Success deletes only this run's fixture databases/user (not recoverable).
Failure stops without automatic retry or cleanup and prints both confirmed
fixtures and attempted creations whose outcomes may be unknown. Inspect those
names before cleanup; rerunning does not clean up previous runs. Internal or
connection errors are never accepted as expected privilege/name errors.

`test_gis_extension_server.py` / CTest `plugin_gis_extension_runner` checks the
runner's validation, sequencing and safeguards offline. **Passing that test is
not evidence that this acceptance command passed against a server.**

## SQL Extension migration status

The [`sql/`](sql/README.md) package covers 82 SQL names with 106 native declarations.
CMake installs `gis.control` and `gis--1.0.sql` into `share/seekdb/extension`
without pulling the optional DSO into the core build. The actual GIS module's
function descriptors are now implementation-only; no test-only name-hiding
switch is needed. Type/cast registration remains module-scoped for ABI support.

Keyword constructors, aliases and ordinary functions use database routine
identities. The GIS regression stages the actual package declarations with
controlled IDs/storage/grants, then uses real SELECT resolution and execution.
It includes an empty database, explicit cross-database qualification, nested
native calls, EXECUTE revocation and independent alias deletion. This does not
prove durable `CREATE EXTENSION`, restart recovery or failure rollback on a
server; those remain release gates. Existing installations using module-global
names require a database SQL installation, not merely replacement of the DSO.
