#!/usr/bin/python3
import pymysql
conn = pymysql.connect(host="11.124.5.18",
                       port=25200,
                       user="root@liuhanyi",
                       passwd="",
                       db="test")
cur = conn.cursor()
def create_table(col_cnt):
    cur.execute("drop table if exists big_table")
    # Create table big_table
    create_sql = "create table big_table (pk int primary key"
    i = col_cnt
    while (i > 0):
        create_sql = create_sql + ', f' + str(i) + " varchar(4096)"
        i = i - 1
    create_sql = create_sql + ") DEFAULT CHARSET = binary"
    cur.execute(create_sql)
def insert_row(col_cnt, value):
    i = col_cnt
    insert_sql = "insert into big_table values(1"
    while (i > 0):
        insert_sql = insert_sql + ", \'" + value + "\'"
        i = i - 1
    insert_sql = insert_sql + ")"
    cur.execute(insert_sql)
def update_row(col_cnt, new_value):
    i = col_cnt
    update_sql = "update big_table set "
    while (i > 0):
        if i == col_cnt :
            update_sql = update_sql + 'f' + str(i) + " = \'" + new_value + "\'"
        else :
            update_sql = update_sql + ', f' + str(i) + " = \'" + new_value + "\'"
        i = i - 1
    update_sql = update_sql + ' where pk = 1'
    cur.execute(update_sql)
try:
    # 1.5MB / 4k = 384
    # 4kb size for each column
    col_cnt = 383
    create_table(col_cnt)
    # 4k / 15  = 273
    val_cnt = 273
    value = '1'
    new_value = '7'
    byte = '111111111111111'    # 15
    
    i = val_cnt
    while (i > 0):
        value = value + byte
        new_value = new_value + byte
        i = i - 1
    
    insert_row(col_cnt, value)
    update_row(col_cnt, new_value)
finally:
    cur.close()
    conn.close()
