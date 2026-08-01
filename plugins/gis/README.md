# seekdb GIS plugin

This package publishes the GIS SQL surface through the execution SPI and returns
host-owned geometry payloads. It provides `ST_Point`, `ST_MakeEnvelope` and
2D/3D `ST_MakePoint`, plus byte-oriented `ST_X`, `ST_Y`,
`ST_SRID`, `ST_AsWKB`, `ST_AsBinary`, `_ST_GeometryType`, `ST_IsValid`,
`ST_AsText`, `ST_AsWKT`, `ST_GeomFromWKB` and `ST_GeometryFromWKB`
accessors, plus `_ST_SetSRID` geometry metadata mutation, `ST_Area`, `ST_Length`,
`ST_Distance`, WKT/GeoJSON constructors, collection constructors, spatial
indexes, relations, transforms, topology/buffer/MVT operations, GeoHash and a
stable Morton spatial-cell key. The extension catalog currently publishes 65
function descriptors.
`ST_Point` takes two non-null little-endian
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
