/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#include <jni.h>
#include <stdlib.h>
#include <string.h>

#include "seekdb.h"

#define BUF_SIZE 4096

static void throw_seekdb_exception(JNIEnv *env, const char *msg) {
    jclass ex_class = (*env)->FindClass(env, "seekdb/SeekdbException");
    if (ex_class) {
        (*env)->ThrowNew(env, ex_class, msg);
    }
}

JNIEXPORT void JNICALL Java_seekdb_Seekdb_open(JNIEnv *env, jclass clazz, jstring db_dir) {
    const char *db_dir_utf = (*env)->GetStringUTFChars(env, db_dir, NULL);
    if (!db_dir_utf) return;

    int ret = seekdb_open(db_dir_utf);
    (*env)->ReleaseStringUTFChars(env, db_dir, db_dir_utf);

    if (ret != SEEKDB_SUCCESS) {
        const char *err = seekdb_last_error();
        throw_seekdb_exception(env, err ? err : "seekdb_open failed");
    }
}

JNIEXPORT void JNICALL Java_seekdb_Seekdb_close(JNIEnv *env, jclass clazz) {
    seekdb_close();
}

JNIEXPORT void JNICALL Java_seekdb_SeekdbConnection_connect(JNIEnv *env, jobject thiz,
        jstring database, jboolean autocommit) {
    const char *db_utf = (*env)->GetStringUTFChars(env, database, NULL);
    if (!db_utf) return;

    SeekdbHandle handle = NULL;
    int ret = seekdb_connect(&handle, db_utf, (autocommit == JNI_TRUE));
    (*env)->ReleaseStringUTFChars(env, database, db_utf);

    if (ret != SEEKDB_SUCCESS) {
        const char *err = seekdb_last_error();
        throw_seekdb_exception(env, err ? err : "seekdb_connect failed");
        return;
    }

    jclass conn_class = (*env)->GetObjectClass(env, thiz);
    jmethodID set_handle = (*env)->GetMethodID(env, conn_class, "setHandle", "(J)V");
    if (set_handle) {
        (*env)->CallVoidMethod(env, thiz, set_handle, (jlong)(intptr_t)handle);
    }
}

JNIEXPORT void JNICALL Java_seekdb_SeekdbConnection_close(JNIEnv *env, jobject thiz) {
    jclass conn_class = (*env)->GetObjectClass(env, thiz);
    jmethodID get_handle = (*env)->GetMethodID(env, conn_class, "getHandle", "()J");
    jmethodID set_handle = (*env)->GetMethodID(env, conn_class, "setHandle", "(J)V");
    if (!get_handle || !set_handle) return;

    jlong h = (*env)->CallLongMethod(env, thiz, get_handle);
    SeekdbHandle handle = (SeekdbHandle)(intptr_t)h;
    if (handle) {
        seekdb_connect_close(handle);
        (*env)->CallVoidMethod(env, thiz, set_handle, (jlong)0);
    }
}

JNIEXPORT jobject JNICALL Java_seekdb_SeekdbConnection_query(JNIEnv *env, jobject thiz, jstring sql) {
    jclass conn_class = (*env)->GetObjectClass(env, thiz);
    jmethodID get_handle = (*env)->GetMethodID(env, conn_class, "getHandle", "()J");
    if (!get_handle) return NULL;

    jlong h = (*env)->CallLongMethod(env, thiz, get_handle);
    SeekdbHandle handle = (SeekdbHandle)(intptr_t)h;
    if (!handle) {
        throw_seekdb_exception(env, "Not connected");
        return NULL;
    }

    const char *sql_utf = (*env)->GetStringUTFChars(env, sql, NULL);
    if (!sql_utf) return NULL;

    SeekdbResult result = NULL;
    int ret = seekdb_query(handle, sql_utf, &result);
    (*env)->ReleaseStringUTFChars(env, sql, sql_utf);

    if (ret != SEEKDB_SUCCESS) {
        const char *err = seekdb_error(handle);
        throw_seekdb_exception(env, err ? err : "seekdb_query failed");
        return NULL;
    }

    result = seekdb_store_result(handle);
    if (!result) {
        throw_seekdb_exception(env, "Result is null");
        return NULL;
    }

    my_ulonglong row_count = seekdb_num_rows(result);
    unsigned int col_count = seekdb_num_fields(result);

    jclass result_class = (*env)->FindClass(env, "seekdb/SeekdbResult");
    if (!result_class) return NULL;
    jmethodID result_init = (*env)->GetMethodID(env, result_class, "<init>", "()V");
    jmethodID set_row_count = (*env)->GetMethodID(env, result_class, "setRowCount", "(J)V");
    jmethodID set_col_count = (*env)->GetMethodID(env, result_class, "setColumnCount", "(I)V");
    jmethodID set_columns = (*env)->GetMethodID(env, result_class, "setColumns", "([Ljava/lang/String;)V");
    jmethodID set_rows = (*env)->GetMethodID(env, result_class, "setRows", "([[Ljava/lang/String;)V");
    if (!result_init || !set_row_count || !set_col_count || !set_columns || !set_rows) {
        seekdb_result_free(result);
        return NULL;
    }

    jobject result_obj = (*env)->NewObject(env, result_class, result_init);
    if (!result_obj) {
        seekdb_result_free(result);
        return NULL;
    }

    (*env)->CallVoidMethod(env, result_obj, set_row_count, (jlong)row_count);
    (*env)->CallVoidMethod(env, result_obj, set_col_count, (jint)col_count);

    /* Build column names array */
    jobjectArray columns = (*env)->NewObjectArray(env, (jsize)col_count, (*env)->FindClass(env, "java/lang/String"), NULL);
    if (columns) {
        char name_buf[256];
        for (unsigned int i = 0; i < col_count; i++) {
            int r = seekdb_result_column_name(result, (int32_t)i, name_buf, sizeof(name_buf));
            jstring col_name;
            if (r == SEEKDB_SUCCESS && name_buf[0]) {
                col_name = (*env)->NewStringUTF(env, name_buf);
            } else {
                char def[64];
                snprintf(def, sizeof(def), "col_%u", i);
                col_name = (*env)->NewStringUTF(env, def);
            }
            if (col_name) {
                (*env)->SetObjectArrayElement(env, columns, (jsize)i, col_name);
            }
        }
        (*env)->CallVoidMethod(env, result_obj, set_columns, columns);
    }

    /* Build rows array */
    jobjectArray rows = (*env)->NewObjectArray(env, (jsize)row_count, (*env)->FindClass(env, "[Ljava/lang/String;"), NULL);
    if (rows) {
        char val_buf[BUF_SIZE];
        for (my_ulonglong i = 0; i < row_count; i++) {
            SeekdbRow row = seekdb_fetch_row(result);
            if (!row) break;

            jobjectArray row_arr = (*env)->NewObjectArray(env, (jsize)col_count, (*env)->FindClass(env, "java/lang/String"), NULL);
            if (row_arr) {
                for (unsigned int j = 0; j < col_count; j++) {
                    if (seekdb_row_is_null(row, (int32_t)j)) {
                        (*env)->SetObjectArrayElement(env, row_arr, (jsize)j, NULL);
                    } else {
                        memset(val_buf, 0, BUF_SIZE);
                        int r = seekdb_row_get_string(row, (int32_t)j, val_buf, BUF_SIZE);
                        if (r == SEEKDB_SUCCESS) {
                            jstring val = (*env)->NewStringUTF(env, val_buf);
                            if (val) {
                                (*env)->SetObjectArrayElement(env, row_arr, (jsize)j, val);
                            }
                        }
                    }
                }
                (*env)->SetObjectArrayElement(env, rows, (jsize)i, row_arr);
            }
        }
        (*env)->CallVoidMethod(env, result_obj, set_rows, rows);
    }

    seekdb_result_free(result);
    return result_obj;
}

JNIEXPORT jlong JNICALL Java_seekdb_SeekdbConnection_executeUpdate(JNIEnv *env, jobject thiz, jstring sql) {
    jclass conn_class = (*env)->GetObjectClass(env, thiz);
    jmethodID get_handle = (*env)->GetMethodID(env, conn_class, "getHandle", "()J");
    if (!get_handle) return 0;

    jlong h = (*env)->CallLongMethod(env, thiz, get_handle);
    SeekdbHandle handle = (SeekdbHandle)(intptr_t)h;
    if (!handle) {
        throw_seekdb_exception(env, "Not connected");
        return 0;
    }

    const char *sql_utf = (*env)->GetStringUTFChars(env, sql, NULL);
    if (!sql_utf) return 0;

    SeekdbResult result = NULL;
    int ret = seekdb_query(handle, sql_utf, &result);
    (*env)->ReleaseStringUTFChars(env, sql, sql_utf);

    if (ret != SEEKDB_SUCCESS) {
        const char *err = seekdb_error(handle);
        throw_seekdb_exception(env, err ? err : "seekdb_query failed");
        return 0;
    }

    if (result) {
        seekdb_result_free(result);
    }

    return (jlong)seekdb_affected_rows(handle);
}

JNIEXPORT void JNICALL Java_seekdb_SeekdbConnection_begin(JNIEnv *env, jobject thiz) {
    jclass conn_class = (*env)->GetObjectClass(env, thiz);
    jmethodID get_handle = (*env)->GetMethodID(env, conn_class, "getHandle", "()J");
    if (!get_handle) return;

    jlong h = (*env)->CallLongMethod(env, thiz, get_handle);
    SeekdbHandle handle = (SeekdbHandle)(intptr_t)h;
    if (!handle) {
        throw_seekdb_exception(env, "Not connected");
        return;
    }

    int ret = seekdb_begin(handle);
    if (ret != SEEKDB_SUCCESS) {
        const char *err = seekdb_error(handle);
        throw_seekdb_exception(env, err ? err : "seekdb_begin failed");
    }
}

JNIEXPORT void JNICALL Java_seekdb_SeekdbConnection_commit(JNIEnv *env, jobject thiz) {
    jclass conn_class = (*env)->GetObjectClass(env, thiz);
    jmethodID get_handle = (*env)->GetMethodID(env, conn_class, "getHandle", "()J");
    if (!get_handle) return;

    jlong h = (*env)->CallLongMethod(env, thiz, get_handle);
    SeekdbHandle handle = (SeekdbHandle)(intptr_t)h;
    if (!handle) {
        throw_seekdb_exception(env, "Not connected");
        return;
    }

    int ret = seekdb_commit(handle);
    if (ret != SEEKDB_SUCCESS) {
        const char *err = seekdb_last_error();
        throw_seekdb_exception(env, err ? err : "seekdb_commit failed");
    }
}

JNIEXPORT void JNICALL Java_seekdb_SeekdbConnection_rollback(JNIEnv *env, jobject thiz) {
    jclass conn_class = (*env)->GetObjectClass(env, thiz);
    jmethodID get_handle = (*env)->GetMethodID(env, conn_class, "getHandle", "()J");
    if (!get_handle) return;

    jlong h = (*env)->CallLongMethod(env, thiz, get_handle);
    SeekdbHandle handle = (SeekdbHandle)(intptr_t)h;
    if (!handle) {
        throw_seekdb_exception(env, "Not connected");
        return;
    }

    int ret = seekdb_rollback(handle);
    if (ret != SEEKDB_SUCCESS) {
        const char *err = seekdb_last_error();
        throw_seekdb_exception(env, err ? err : "seekdb_rollback failed");
    }
}

JNIEXPORT jstring JNICALL Java_seekdb_SeekdbConnection_getLastError(JNIEnv *env, jobject thiz) {
    jclass conn_class = (*env)->GetObjectClass(env, thiz);
    jmethodID get_handle = (*env)->GetMethodID(env, conn_class, "getHandle", "()J");
    if (!get_handle) return NULL;

    jlong h = (*env)->CallLongMethod(env, thiz, get_handle);
    SeekdbHandle handle = (SeekdbHandle)(intptr_t)h;
    if (!handle) return NULL;

    const char *err = seekdb_error(handle);
    if (err) {
        return (*env)->NewStringUTF(env, err);
    }
    return NULL;
}
