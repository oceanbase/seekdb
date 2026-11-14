-- Copyright (c) 2026 OceanBase.
--
-- Licensed under the Apache License, Version 2.0 (the "License");
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at
--
--     http://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an "AS IS" BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

-- Run against a disposable schema with secure_file_priv permitting /tmp.
-- These exclusive output paths must not exist before this fixture runs.
CREATE TABLE file_facade_source (id INT PRIMARY KEY, payload LONGTEXT);
INSERT INTO file_facade_source VALUES
  (1, CONCAT('prefix\t\n\\', REPEAT('a', 1048593))),
  (2, REPEAT('b', 1048613)),
  (3, 'last');
SELECT id, payload INTO OUTFILE '/tmp/file-facade-plain.csv'
  FIELDS TERMINATED BY '\t' LINES TERMINATED BY '\n'
  FROM file_facade_source ORDER BY id;
CREATE TABLE file_facade_plain LIKE file_facade_source;
LOAD DATA INFILE '/tmp/file-facade-plain.csv' INTO TABLE file_facade_plain
  FIELDS TERMINATED BY '\t' LINES TERMINATED BY '\n';
SELECT COUNT(*) AS plain_mismatches FROM file_facade_source s
  LEFT JOIN file_facade_plain d USING(id)
  WHERE d.id IS NULL OR BINARY s.payload != BINARY d.payload;
SELECT id, payload INTO OUTFILE '/tmp/file-facade-gzip.csv.gz'
  FORMAT=(TYPE='CSV', COMPRESSION='GZIP', FIELD_DELIMITER='\t', LINE_DELIMITER='\n')
  FROM file_facade_source ORDER BY id;
CREATE TABLE file_facade_gzip LIKE file_facade_source;
LOAD DATA INFILE '/tmp/file-facade-gzip.csv.gz' INTO TABLE file_facade_gzip
  COMPRESSION='GZIP' FIELDS TERMINATED BY '\t' LINES TERMINATED BY '\n';
SELECT COUNT(*) AS gzip_mismatches FROM file_facade_source s
  LEFT JOIN file_facade_gzip d USING(id)
  WHERE d.id IS NULL OR BINARY s.payload != BINARY d.payload;
SELECT UNHEX('6100620A') INTO DUMPFILE '/tmp/file-facade-binary.dump';
SELECT UNHEX('006162') INTO DUMPFILE '/tmp/file-facade-leading-zero.dump';
SELECT UNHEX('616200') INTO DUMPFILE '/tmp/file-facade-trailing-zero.dump';
CREATE TABLE file_facade_dump (payload VARBINARY(16));
LOAD DATA INFILE '/tmp/file-facade-binary.dump' INTO TABLE file_facade_dump
  FIELDS TERMINATED BY '!' ESCAPED BY '' LINES TERMINATED BY '!';
LOAD DATA INFILE '/tmp/file-facade-leading-zero.dump' INTO TABLE file_facade_dump
  FIELDS TERMINATED BY '!' ESCAPED BY '' LINES TERMINATED BY '!';
LOAD DATA INFILE '/tmp/file-facade-trailing-zero.dump' INTO TABLE file_facade_dump
  FIELDS TERMINATED BY '!' ESCAPED BY '' LINES TERMINATED BY '!';
SELECT HEX(payload) AS binary_dump_hex FROM file_facade_dump ORDER BY payload;
SELECT id, payload INTO OUTFILE '/tmp/file-facade-split.csv'
  FIELDS TERMINATED BY '\t' LINES TERMINATED BY '\n'
  SINGLE=FALSE MAX_FILE_SIZE='1M'
  FROM file_facade_source ORDER BY id;
DROP TABLE file_facade_dump, file_facade_gzip, file_facade_plain, file_facade_source;
