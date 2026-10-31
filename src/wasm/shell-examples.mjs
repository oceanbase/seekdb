/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

export const EXAMPLES = Object.freeze({
  hybrid: `CREATE DATABASE IF NOT EXISTS playground;
USE playground;
DROP TABLE IF EXISTS shell_hybrid_demo;
CREATE TABLE shell_hybrid_demo (
  id INT PRIMARY KEY,
  body VARCHAR(200),
  category VARCHAR(20),
  embedding VECTOR(3),
  FULLTEXT INDEX body_ft(body),
  VECTOR INDEX embedding_idx(embedding)
    WITH (distance=l2, type=hnsw, lib=vsag)
);
INSERT INTO shell_hybrid_demo VALUES
  (1, 'database vector search', 'docs', '[1,0,0]'),
  (2, 'database full text search', 'docs', '[1,1,0]'),
  (3, 'image classification guide', 'docs', '[1,0,0]'),
  (4, 'database pricing', 'news', '[1,0.1,0]');
SELECT id, body, l2_distance(embedding, '[1,0,0]') AS distance
FROM shell_hybrid_demo
WHERE MATCH(body) AGAINST('database') AND category = 'docs'
ORDER BY distance APPROXIMATE LIMIT 3;`,

  fork: `CREATE DATABASE IF NOT EXISTS playground;
USE playground;
DROP TABLE IF EXISTS shell_fork_draft;
DROP TABLE IF EXISTS shell_fork_notes;
CREATE TABLE shell_fork_notes (
  id INT PRIMARY KEY,
  title VARCHAR(80)
);
INSERT INTO shell_fork_notes VALUES
  (1, 'Original title'),
  (2, 'Shared title');
FORK TABLE shell_fork_notes TO shell_fork_draft;
UPDATE shell_fork_draft SET title = 'Edited in fork' WHERE id = 1;
UPDATE shell_fork_notes SET title = 'Edited in source' WHERE id = 2;
SELECT 'source' AS table_copy, id, title FROM shell_fork_notes ORDER BY id;
SELECT 'fork' AS table_copy, id, title FROM shell_fork_draft ORDER BY id;`,
});
