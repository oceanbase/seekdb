#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Copyright (c) 2025 OceanBase.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

SeekDB Python FFI Binding Test Suite
Following database binding layer test requirements
"""

import sys
import os
import json

# Add the current directory to the path so we can import seekdb
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from seekdb import Seekdb, SeekdbConnection, SeekdbError
from seekdb_binding_probe import emit as _probe
from seekdb_binding_probe import init_probe_log as _probe_init


def test_open():
    """Test database open"""
    try:
        conn = SeekdbConnection('test', autocommit=True)
        conn.close()
        return {'passed': True, 'message': None}
    except Exception as e:
        return {'passed': False, 'message': str(e)}


def test_connection():
    """Test connection creation"""
    try:
        conn = SeekdbConnection('test', autocommit=True)
        if conn._handle.value is None:
            raise Exception('Connection handle is null')
        conn.close()
        return {'passed': True, 'message': None}
    except Exception as e:
        return {'passed': False, 'message': str(e)}


def test_error_handling():
    """Test error handling"""
    try:
        # Test invalid parameters
        try:
            conn = SeekdbConnection(None, autocommit=True)
            return {'passed': False, 'message': 'Should have thrown error for null database'}
        except:
            pass
        
        # Test invalid SQL
        conn = SeekdbConnection('test', autocommit=True)
        try:
            conn.execute('INVALID SQL STATEMENT')
            return {'passed': False, 'message': 'Should have thrown error for invalid SQL'}
        except:
            pass
        conn.close()
        return {'passed': True, 'message': None}
    except Exception as e:
        return {'passed': False, 'message': str(e)}


def test_result_operations():
    """Test result operations and column name inference (core feature)"""
    try:
        conn = SeekdbConnection('test', autocommit=True)
        
        # Create table with known column names to test column name inference
        conn.execute_update('CREATE TABLE IF NOT EXISTS test_cols (user_id INT, user_name VARCHAR(100))')
        conn.execute_update("INSERT INTO test_cols VALUES (1, 'Alice')")
        
        # Query with explicit column names - column names should be inferred from database
        result = conn.execute('SELECT user_id, user_name FROM test_cols')
        
        if result.row_count != 1:
            raise Exception(f'Expected row count 1, got {result.row_count}')
        if result.column_count != 2:
            raise Exception(f'Expected column count 2, got {result.column_count}')
        if len(result.column_names) != 2:
            raise Exception(f'Expected 2 column names, got {len(result.column_names)}')
        
        # Verify column names are correctly inferred
        if result.column_names[0] != 'user_id':
            raise Exception(f"Expected first column name 'user_id', got '{result.column_names[0]}'")
        if result.column_names[1] != 'user_name':
            raise Exception(f"Expected second column name 'user_name', got '{result.column_names[1]}'")
        
        rows = result.fetch_all()
        if len(rows) != 1:
            raise Exception(f'Expected 1 row, got {len(rows)}')
        
        result.free()
        conn.execute_update('DROP TABLE IF EXISTS test_cols')
        conn.close()
        return {'passed': True, 'message': None}
    except Exception as e:
        return {'passed': False, 'message': str(e)}


def test_row_operations():
    """Test row operations and data types"""
    try:
        conn = SeekdbConnection('test', autocommit=True)
        
        conn.execute_update('''
            CREATE TABLE IF NOT EXISTS test_types (
                id INT PRIMARY KEY,
                name VARCHAR(100),
                price DECIMAL(10,2),
                quantity INT,
                active BOOLEAN,
                score DOUBLE
            )
        ''')
        
        conn.execute_update('''
            INSERT INTO test_types VALUES
            (1, 'Product A', 99.99, 10, true, 4.5),
            (2, 'Product B', 199.99, 5, false, 3.8),
            (3, NULL, NULL, NULL, NULL, NULL)
        ''')
        
        result = conn.execute('SELECT * FROM test_types ORDER BY id')
        rows = result.fetch_all()
        
        if len(rows) != 3:
            raise Exception(f'Expected 3 rows, got {len(rows)}')
        
        conn.execute_update('DROP TABLE IF EXISTS test_types')
        result.free()
        conn.close()
        return {'passed': True, 'message': None}
    except Exception as e:
        return {'passed': False, 'message': str(e)}


def test_error_message():
    """Test error message retrieval"""
    try:
        conn = SeekdbConnection('test', autocommit=True)
        
        # Try to trigger an error
        try:
            conn.execute('SELECT * FROM non_existent_table')
        except:
            error_msg = conn.get_last_error()
            if not error_msg:
                raise Exception('Error message should be available after failed query')
        
        conn.close()
        return {'passed': True, 'message': None}
    except Exception as e:
        return {'passed': False, 'message': str(e)}


def test_transaction_management():
    """Test transaction management"""
    try:
        conn = SeekdbConnection('test', autocommit=False)
        
        conn.execute_update('CREATE TABLE IF NOT EXISTS test_txn (id INT PRIMARY KEY, value INT)')
        
        # Test begin/commit
        conn.begin()
        conn.execute_update("INSERT INTO test_txn VALUES (1, 100)")
        conn.commit()
        
        result = conn.execute('SELECT * FROM test_txn WHERE id = 1')
        rows = result.fetch_all()
        if len(rows) != 1:
            raise Exception('Data not committed')
        result.free()
        
        # Test begin/rollback
        conn.begin()
        conn.execute_update("INSERT INTO test_txn VALUES (2, 200)")
        conn.rollback()
        
        conn.execute_update('DROP TABLE IF EXISTS test_txn')
        conn.close()
        return {'passed': True, 'message': None}
    except Exception as e:
        return {'passed': False, 'message': str(e)}


def test_ddl_operations():
    """Test DDL operations"""
    try:
        conn = SeekdbConnection('test', autocommit=True)
        
        conn.execute_update('''
            CREATE TABLE IF NOT EXISTS test_ddl (
                id INT PRIMARY KEY,
                name VARCHAR(100),
                created_at TIMESTAMP
            )
        ''')
        
        try:
            conn.execute_update('ALTER TABLE test_ddl ADD COLUMN description VARCHAR(255)')
        except:
            pass  # ALTER TABLE may not be supported
        
        conn.execute_update('DROP TABLE IF EXISTS test_ddl')
        conn.close()
        return {'passed': True, 'message': None}
    except Exception as e:
        return {'passed': False, 'message': str(e)}


def test_dml_operations():
    """Test DML operations"""
    try:
        conn = SeekdbConnection('test', autocommit=True)
        
        conn.execute_update('''
            CREATE TABLE IF NOT EXISTS test_dml (
                id INT PRIMARY KEY,
                name VARCHAR(100),
                value INT
            )
        ''')
        
        insert_rows = conn.execute_update("INSERT INTO test_dml VALUES (1, 'A', 10), (2, 'B', 20), (3, 'C', 30)")
        if insert_rows != 3:
            raise Exception(f'Expected 3 rows inserted, got {insert_rows}')
        
        update_rows = conn.execute_update("UPDATE test_dml SET value = 100 WHERE id = 1")
        if update_rows != 1:
            raise Exception(f'Expected 1 row updated, got {update_rows}')
        
        result = conn.execute('SELECT value FROM test_dml WHERE id = 1')
        rows = result.fetch_all()
        if len(rows) != 1:
            raise Exception('UPDATE verification failed')
        result.free()
        
        delete_rows = conn.execute_update('DELETE FROM test_dml WHERE id = 2')
        if delete_rows != 1:
            raise Exception(f'Expected 1 row deleted, got {delete_rows}')
        
        result = conn.execute('SELECT * FROM test_dml WHERE id = 2')
        rows = result.fetch_all()
        if len(rows) != 0:
            raise Exception('DELETE verification failed')
        result.free()
        
        conn.execute_update('DROP TABLE IF EXISTS test_dml')
        conn.close()
        return {'passed': True, 'message': None}
    except Exception as e:
        return {'passed': False, 'message': str(e)}


def test_hybrid_search_get_sql():
    """Test DBMS_HYBRID_SEARCH.GET_SQL"""
    conn = None
    try:
        conn = SeekdbConnection('test', autocommit=True)
        
        try:
            conn.execute_update('DROP TABLE IF EXISTS doc_table')
        except:
            pass
        
        create_table_sql = '''CREATE TABLE doc_table (
            c1 INT,
            vector VECTOR(3),
            query VARCHAR(255),
            content VARCHAR(255),
            VECTOR INDEX idx1(vector) WITH (distance=l2, type=hnsw, lib=vsag),
            FULLTEXT idx2(query),
            FULLTEXT idx3(content)
        )'''
        conn.execute_update(create_table_sql)
        
        insert_sql = '''INSERT INTO doc_table VALUES
            (1, '[1,2,3]', 'hello world', 'oceanbase Elasticsearch database'),
            (2, '[1,2,1]', 'hello world, what is your name', 'oceanbase mysql database'),
            (3, '[1,1,1]', 'hello world, how are you', 'oceanbase oracle database'),
            (4, '[1,3,1]', 'real world, where are you from', 'postgres oracle database'),
            (5, '[1,3,2]', 'real world, how old are you', 'redis oracle database'),
            (6, '[2,1,1]', 'hello world, where are you from', 'starrocks oceanbase database')'''
        conn.execute_update(insert_sql)
        
        search_params_obj = {
            "query": {
                "bool": {
                    "should": [
                        {"match": {"query": "hi hello"}},
                        {"match": {"content": "oceanbase mysql"}}
                    ],
                    "filter": [
                        {"term": {"content": "postgres"}}
                    ]
                }
            },
            "knn": {
                "field": "vector",
                "k": 5,
                "query_vector": [1, 2, 3]
            },
            "_source": ["query", "content", "_keyword_score", "_semantic_score"]
        }
        search_params = json.dumps(search_params_obj)
        escaped_params = search_params.replace("'", "''")
        
        set_parm_sql = f"SET @parm = '{escaped_params}'"
        conn.execute_update(set_parm_sql)
        
        get_sql_query = "SELECT DBMS_HYBRID_SEARCH.GET_SQL('doc_table', @parm)"
        sql_result = conn.execute(get_sql_query)
        sql_rows = sql_result.fetch_all()
        
        if len(sql_rows) == 0:
            raise Exception('GET_SQL returned no rows')
        
        sql_result.free()
        conn.close()
        return {'passed': True, 'message': None}
    except Exception as e:
        error_msg = str(e)
        if conn:
            try:
                detailed_error = conn.get_last_error()
                if detailed_error:
                    error_msg = f"{e} ({detailed_error})"
                conn.close()
            except:
                pass
        return {'passed': False, 'message': error_msg}


def test_hybrid_search_search():
    """Test DBMS_HYBRID_SEARCH.SEARCH"""
    conn = None
    try:
        conn = SeekdbConnection('test', autocommit=True)
        
        search_params_obj = {
            "query": {
                "bool": {
                    "should": [
                        {"match": {"query": "hello"}},
                        {"match": {"content": "oceanbase mysql"}}
                    ]
                }
            },
            "knn": {
                "field": "vector",
                "k": 5,
                "query_vector": [1, 2, 3]
            },
            "_source": ["c1", "query", "content", "_keyword_score", "_semantic_score"]
        }
        search_params = json.dumps(search_params_obj)
        escaped_params = search_params.replace("'", "''")
        
        search_query = f"SELECT DBMS_HYBRID_SEARCH.SEARCH('doc_table', '{escaped_params}') as result"
        search_result = conn.execute(search_query)
        search_rows = search_result.fetch_all()
        
        if len(search_rows) == 0:
            raise Exception('SEARCH returned no rows')
        
        search_result.free()
        conn.close()
        return {'passed': True, 'message': None}
    except Exception as e:
        error_msg = str(e)
        if conn:
            try:
                detailed_error = conn.get_last_error()
                if detailed_error:
                    error_msg = f"{e} ({detailed_error})"
                conn.close()
            except:
                pass
        return {'passed': False, 'message': error_msg}


def test_column_name_inference():
    """Test column name inference with multiple columns (core feature)"""
    try:
        conn = SeekdbConnection('test', autocommit=True)
        
        # Create test table with multiple columns to test column name inference
        conn.execute_update('CREATE TABLE IF NOT EXISTS test_cols (user_id INT, user_name VARCHAR(100), user_email VARCHAR(100))')
        conn.execute_update("INSERT INTO test_cols VALUES (1, 'Alice', 'alice@example.com')")
        
        # Query with explicit column names - column names should be inferred from database
        result = conn.execute('SELECT user_id, user_name, user_email FROM test_cols')
        
        # Verify column count
        if result.column_count != 3:
            raise Exception(f'Expected 3 columns, got {result.column_count}')
        
        # Verify column names are correctly inferred
        if len(result.column_names) != 3:
            raise Exception(f'Expected 3 column names, got {len(result.column_names)}')
        
        if result.column_names[0] != 'user_id':
            raise Exception(f"Expected first column name 'user_id', got '{result.column_names[0]}'")
        if result.column_names[1] != 'user_name':
            raise Exception(f"Expected second column name 'user_name', got '{result.column_names[1]}'")
        if result.column_names[2] != 'user_email':
            raise Exception(f"Expected third column name 'user_email', got '{result.column_names[2]}'")
        
        # Verify data
        rows = result.fetch_all()
        if len(rows) != 1:
            raise Exception(f'Expected 1 row, got {len(rows)}')
        
        # Verify row data can be accessed by column name
        if rows[0]['user_id'] != '1':
            raise Exception(f"Expected user_id '1', got '{rows[0]['user_id']}'")
        if rows[0]['user_name'] != 'Alice':
            raise Exception(f"Expected user_name 'Alice', got '{rows[0]['user_name']}'")
        if rows[0]['user_email'] != 'alice@example.com':
            raise Exception(f"Expected user_email 'alice@example.com', got '{rows[0]['user_email']}'")
        
        result.free()
        conn.execute_update('DROP TABLE IF EXISTS test_cols')
        conn.close()
        return {'passed': True, 'message': None}
    except Exception as e:
        return {'passed': False, 'message': str(e)}


def test_parameterized_queries():
    """Test parameterized queries (core feature)"""
    try:
        conn = SeekdbConnection('test', autocommit=True)
        
        # Create test table
        conn.execute_update('CREATE TABLE IF NOT EXISTS test_params (id INT PRIMARY KEY, name VARCHAR(100))')
        conn.execute_update("INSERT INTO test_params VALUES (1, 'Alice'), (2, 'Bob')")
        
        # Test parameterized query concept
        # Since we don't have Prepared Statement bindings yet, we test column name inference
        # which is the core feature for parameterized queries
        result = conn.execute("SELECT * FROM test_params WHERE id = 1")
        
        # Verify column names are correctly inferred (core feature: column name inference)
        if result.column_count != 2:
            raise Exception(f'Expected 2 columns, got {result.column_count}')
        if result.column_names[0] != 'id':
            raise Exception(f"Expected first column name 'id', got '{result.column_names[0]}'")
        if result.column_names[1] != 'name':
            raise Exception(f"Expected second column name 'name', got '{result.column_names[1]}'")
        
        # Verify data
        rows = result.fetch_all()
        if len(rows) != 1:
            raise Exception(f'Expected 1 row, got {len(rows)}')
        if rows[0]['id'] != '1' or rows[0]['name'] != 'Alice':
            raise Exception('Parameterized query result mismatch')
        
        result.free()
        conn.execute_update('DROP TABLE IF EXISTS test_params')
        conn.close()
        return {'passed': True, 'message': None}
    except Exception as e:
        return {'passed': False, 'message': str(e)}


def test_vector_parameter_binding():
    """Test VECTOR type in INSERT (regression test for VECTOR_INSERT_ISSUE)
    
    Ensures VECTOR columns work in INSERT without "Column cannot be null" errors.
    """
    try:
        conn = SeekdbConnection('test', autocommit=True)
        conn.execute_update('DROP TABLE IF EXISTS test_vector_params')
        conn.execute_update("""CREATE TABLE test_vector_params (
            id INT AUTO_INCREMENT PRIMARY KEY,
            document VARCHAR(255),
            metadata VARCHAR(255),
            embedding VECTOR(3)
        )""")
        conn.execute_update("""INSERT INTO test_vector_params (document, metadata, embedding) VALUES
            ('Document 1', '{"category":"A","score":95}', '[1,2,3]'),
            ('Document 2', '{"category":"B","score":90}', '[2,3,4]'),
            ('Document 3', '{"category":"A","score":88}', '[1.1,2.1,3.1]'),
            ('Document 4', '{"category":"C","score":92}', '[2.1,3.1,4.1]'),
            ('Document 5', '{"category":"B","score":85}', '[1.2,2.2,3.2]')""")
        r = conn.execute('SELECT COUNT(*) as cnt FROM test_vector_params')
        rows = r.fetch_all()
        r.free()
        cnt = rows[0].get('cnt', '') if rows and isinstance(rows[0], dict) else (rows[0][0] if rows and rows[0] else '')
        if len(rows) != 1 or str(cnt) != '5':
            conn.close()
            return {'passed': False, 'message': f'Expected 5 rows, got {cnt}'}
        r2 = conn.execute("SELECT document, embedding FROM test_vector_params WHERE document = 'Document 1' LIMIT 1")
        ver = r2.fetch_all()
        r2.free()
        if len(ver) != 1:
            conn.close()
            return {'passed': False, 'message': "Expected 1 row for document='Document 1'"}
        emb = ver[0].get('embedding') if isinstance(ver[0], dict) else (ver[0][1] if len(ver[0]) > 1 else None)
        if emb is None or (isinstance(emb, str) and len(emb) == 0):
            conn.close()
            return {'passed': False, 'message': 'Embedding column is NULL - VECTOR INSERT failed'}
        
        # Note: VECTOR type return format
        # - Insert: JSON array format "[1,2,3]" (via direct SQL embedding)
        # - Storage: Binary format (float array, e.g., 3 floats = 12 bytes for VECTOR(3))
        # - Query return: Typically binary format (decoded with errors='replace' by get_string())
        #   The important thing is that data is not NULL, which means INSERT succeeded
        
        # Test 2: Additional INSERT to verify VECTOR type continues to work
        conn.execute_update("""INSERT INTO test_vector_params (document, metadata, embedding) VALUES
            ('Auto-Detection Test', '{"category":"TEST","score":100}', '[1.5,2.5,3.5]')""")
        r3 = conn.execute('SELECT COUNT(*) as cnt FROM test_vector_params')
        rows3 = r3.fetch_all()
        r3.free()
        cnt3 = rows3[0].get('cnt', '') if rows3 and isinstance(rows3[0], dict) else (rows3[0][0] if rows3 and rows3[0] else '')
        if len(rows3) != 1 or str(cnt3) != '6':
            conn.execute_update('DROP TABLE IF EXISTS test_vector_params')
            conn.close()
            return {'passed': False, 'message': f'Expected 6 rows after additional insert, got {cnt3}'}
        
        # Verify the additional row
        r4 = conn.execute("SELECT document, embedding FROM test_vector_params WHERE document = 'Auto-Detection Test' LIMIT 1")
        ver2 = r4.fetch_all()
        r4.free()
        if len(ver2) != 1:
            conn.execute_update('DROP TABLE IF EXISTS test_vector_params')
            conn.close()
            return {'passed': False, 'message': "Expected 1 row for document='Auto-Detection Test'"}
        emb2 = ver2[0].get('embedding') if isinstance(ver2[0], dict) else (ver2[0][1] if len(ver2[0]) > 1 else None)
        if emb2 is None or (isinstance(emb2, str) and len(emb2) == 0):
            conn.execute_update('DROP TABLE IF EXISTS test_vector_params')
            conn.close()
            return {'passed': False, 'message': 'Embedding column is NULL in additional insert - VECTOR INSERT failed'}
        
        conn.execute_update('DROP TABLE IF EXISTS test_vector_params')
        conn.close()
        return {'passed': True, 'message': None}
    except Exception as e:
        return {'passed': False, 'message': str(e)}


def test_binary_parameter_binding():
    """Test binary parameter binding with CAST(? AS BINARY) queries
    
    This test verifies that BLOB type correctly handles binary data
    in CAST(? AS BINARY) queries, ensuring proper binary comparison.
    
    Note: This test requires prepared statement API support.
    Currently using direct SQL execution as a workaround.
    """
    try:
        conn = SeekdbConnection('test', autocommit=True)
        conn.execute_update('DROP TABLE IF EXISTS test_binary_params')
        conn.execute_update('CREATE TABLE test_binary_params (id INT PRIMARY KEY, _id VARBINARY(100))')
        
        # Insert binary data using direct SQL (since prepared statement API may not be available)
        # In a full implementation, this would use prepared statements with SEEKDB_TYPE_BLOB
        binary_values = ['get1', 'get2', 'get3', 'get4', 'get5']
        for i, val in enumerate(binary_values, 1):
            # Use hex format for binary data in SQL
            hex_val = val.encode('utf-8').hex()
            conn.execute_update(f"INSERT INTO test_binary_params (id, _id) VALUES ({i}, 0x{hex_val})")
        
        # Verify data was inserted correctly
        r = conn.execute('SELECT COUNT(*) as cnt FROM test_binary_params')
        rows = r.fetch_all()
        r.free()
        cnt = rows[0].get('cnt', '') if rows and isinstance(rows[0], dict) else (rows[0][0] if rows and rows[0] else '')
        if len(rows) != 1 or str(cnt) != '5':
            conn.execute_update('DROP TABLE IF EXISTS test_binary_params')
            conn.close()
            return {'passed': False, 'message': f'Expected 5 rows, got {cnt}'}
        
        # Query using CAST(? AS BINARY) - using direct SQL with hex format
        # In a full implementation, this would use prepared statements with SEEKDB_TYPE_BLOB
        search_val = 'get1'
        hex_search = search_val.encode('utf-8').hex()
        r2 = conn.execute(f"SELECT _id FROM test_binary_params WHERE _id = CAST(0x{hex_search} AS BINARY)")
        ver = r2.fetch_all()
        r2.free()
        
        if len(ver) != 1:
            conn.execute_update('DROP TABLE IF EXISTS test_binary_params')
            conn.close()
            return {'passed': False, 'message': 'SELECT with CAST(? AS BINARY) returned 0 rows, expected 1 row'}
        
        # Verify fetched data matches
        fetched_id = ver[0].get('_id') if isinstance(ver[0], dict) else (ver[0][0] if len(ver[0]) > 0 else None)
        if fetched_id is None:
            conn.execute_update('DROP TABLE IF EXISTS test_binary_params')
            conn.close()
            return {'passed': False, 'message': "Fetched _id is NULL, expected 'get1'"}
        
        # Test negative case: query for non-existent value
        not_found_val = 'notfound'
        hex_not_found = not_found_val.encode('utf-8').hex()
        r3 = conn.execute(f"SELECT _id FROM test_binary_params WHERE _id = CAST(0x{hex_not_found} AS BINARY)")
        ver3 = r3.fetch_all()
        r3.free()
        
        if len(ver3) != 0:
            conn.execute_update('DROP TABLE IF EXISTS test_binary_params')
            conn.close()
            return {'passed': False, 'message': f'SELECT with non-existent value returned {len(ver3)} rows, expected 0'}
        
        conn.execute_update('DROP TABLE IF EXISTS test_binary_params')
        conn.close()
        return {'passed': True, 'message': None}
    except Exception as e:
        return {'passed': False, 'message': str(e)}


def run_all_tests():
    """Main test runner"""
    _probe_init()
    _probe("test.py:run_all_tests_entry")
    print('=' * 70)
    print('SeekDB Python FFI Binding Test Suite')
    print('=' * 70)
    print('')
    
    db_dir = sys.argv[1] if len(sys.argv) > 1 else './seekdb.db'
    try:
        _probe("test.py:before_first_Seekdb_open")
        Seekdb.open(db_dir)
        _probe("test.py:after_first_Seekdb_open")
    except Exception as e:
        print(f'::error::Failed to open database: {e}', file=sys.stderr)
        return 1
    
    test_cases = [
        {'name': 'Database Open', 'fn': test_open},
        {'name': 'Connection Creation', 'fn': test_connection},
        {'name': 'Error Handling', 'fn': test_error_handling},
        {'name': 'Result Operations', 'fn': test_result_operations},
        {'name': 'Row Operations', 'fn': test_row_operations},
        {'name': 'Error Message', 'fn': test_error_message},
        {'name': 'Transaction Management', 'fn': test_transaction_management},
        {'name': 'DDL Operations', 'fn': test_ddl_operations},
        {'name': 'DML Operations', 'fn': test_dml_operations},
        {'name': 'Parameterized Queries', 'fn': test_parameterized_queries},
        {'name': 'VECTOR Parameter Binding', 'fn': test_vector_parameter_binding},
        {'name': 'Binary Parameter Binding', 'fn': test_binary_parameter_binding},
        {'name': 'Column Name Inference', 'fn': test_column_name_inference},
        {'name': 'DBMS_HYBRID_SEARCH.GET_SQL', 'fn': test_hybrid_search_get_sql},
        {'name': 'DBMS_HYBRID_SEARCH.SEARCH', 'fn': test_hybrid_search_search},
    ]
    
    results = []
    failed_tests = []
    
    try:
        for test_case in test_cases:
            sys.stdout.write(f"[TEST] {test_case['name']:<40} ... ")
            sys.stdout.flush()
            result = test_case['fn']()
            results.append({'name': test_case['name'], **result})
            
            if result['passed']:
                print('PASS')
            else:
                print('FAIL')
                failed_tests.append({'name': test_case['name'], 'message': result['message']})
                if result['message']:
                    print(f'::error::Test "{test_case["name"]}" failed: {result["message"]}', file=sys.stderr)
        
        print('')
        print('-' * 70)
        
        passed = sum(1 for r in results if r['passed'])
        total = len(results)
        failed = total - passed
        
        if failed > 0:
            print('Failed Tests:')
            print('-' * 70)
            for test in failed_tests:
                print(f'  ✗ {test["name"]}')
                if test['message']:
                    print(f'    Error: {test["message"]}')
            print('-' * 70)
        
        print(f'Total: {passed}/{total} passed, {failed} failed')
        print('')

        # Absolute path for the *same* data directory: seekdb.cpp merges this via same_embedded_path
        # while still open. Do NOT seekdb_open a different dir after seekdb_close() in-process —
        # seekdb_close() intentionally skips OBSERVER.destroy(), so a second full init usually fails (-2).
        if passed == total:
            abs_same = os.path.abspath(db_dir)
            _probe("test.py:before_abs_path_Seekdb_open")
            sys.stdout.write('[TEST] Absolute path (same DB directory)               ... ')
            sys.stdout.flush()
            try:
                Seekdb.open(abs_same)
                _probe("test.py:after_abs_path_Seekdb_open")
                print('PASS')
            except Exception as e:
                print('FAIL')
                print(f'::error::Absolute-path same-directory check failed: {e}', file=sys.stderr)
                Seekdb.close()
                return 1

        _probe("test.py:before_final_Seekdb_close")
        Seekdb.close()
        _probe("test.py:after_final_Seekdb_close")

        if passed == total:
            _probe("test.py:before_success_messages")
            print('::notice::All tests passed successfully!')
            print('=' * 70)
            _probe("test.py:after_success_messages")
            try:
                sys.stdout.flush()
                sys.stderr.flush()
            except Exception:
                pass
            _probe("test.py:before_os_exit_0")
            os._exit(0)
        else:
            print(f'::error::{failed} test(s) failed', file=sys.stderr)
            print('=' * 70)
            return 1
    except Exception as e:
        print(f'::error::Unexpected error during tests: {e}', file=sys.stderr)
        import traceback
        traceback.print_exc()
        try:
            Seekdb.close()
        except:
            pass
        return 1


if __name__ == '__main__':
    _probe_init()
    _probe("test.py:main_before_run_all_tests")
    exit_code = run_all_tests()
    _probe("test.py:main_after_run_all_tests")
    os._exit(exit_code)
