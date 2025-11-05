#!/usr/bin/env python
# -*- coding: utf-8 -*-

import mysql.connector
from mysql.connector import errorcode
from my_error import MyError
from actions import QueryCursor
import logging

def results_to_str(desc, results):
  ret_str = ''
  max_width_list = []
  for col_desc in desc:
    max_width_list.append(len(str(col_desc[0])))
  col_count = len(max_width_list)
  for result in results:
    if col_count != len(result):
      raise MyError('column count is not equal, desc column count: {0}, data column count: {1}'.format(col_count, len(result)))
    for i in range(0, col_count):
      result_col_width = len(str(result[i]))
      if max_width_list[i] < result_col_width:
        max_width_list[i] = result_col_width
  # Print column names
  for i in range(0, col_count):
    if i > 0:
      ret_str += '    ' # four spaces
    ret_str += str(desc[i][0])
    # Fill in the blanks
    for j in range(0, max_width_list[i] - len(str(desc[i][0]))):
      ret_str += ' '
  # Print data
  for result in results:
    ret_str += '\n' # newline first
    for i in range(0, col_count):
      if i > 0:
        ret_str += '    ' # four spaces
      ret_str += str(result[i])
      # Fill in the blanks
      for j in range(0, max_width_list[i] - len(str(result[i]))):
        ret_str += ' '
  return ret_str

def query_and_dump_results(query_cur, sql):
  (desc, results) = query_cur.exec_query(sql)
  result_str = results_to_str(desc, results)
  logging.info('dump query results, sql: %s, results:\n%s', sql, result_str)

