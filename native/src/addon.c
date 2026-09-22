#include <node_api.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "../include/fastscan.h"
#include "../include/mmap_reader.h"
#include "../include/scanner.h"
#include "../include/platform.h"
#include "../include/thread_pool.h"

static napi_value throw_error(napi_env env, const char* msg) {
    napi_throw_error(env, NULL, msg);
    return NULL;
}

static const char* status_to_str(fs_status_t status) {
    switch (status) {
        case FS_ERROR_OPEN_FAILED: return "File not found";
        case FS_ERROR_MMAP_FAILED: return "Memory mapping failed";
        case FS_ERROR_OUT_OF_BOUNDS: return "Buffer allocation failed";
        case FS_ERROR_INVALID_ARG: return "Invalid argument";
        case FS_ERROR_FILE_TRUNCATED:
        case FS_ERROR_BUS_FAULT: return "File truncated during scan";
        case FS_ERROR_UNSUPPORTED_FILE_TYPE: return "Unsupported file type (directories and special devices not supported)";
        default: return "Unknown Error";
    }
}

static void FreeMatchesCallback(napi_env env, void* data, void* hint) {
    size_t byte_length = (size_t)hint;
    if (byte_length > 0) {
        int64_t adjusted = 0;
        napi_adjust_external_memory(env, -(int64_t)byte_length, &adjusted);
    }
    free(data);
}

static char* extract_filepath(napi_env env, napi_value val) {
    napi_valuetype type;
    if (napi_typeof(env, val, &type) != napi_ok || type != napi_string) return NULL;
    size_t str_len = 0;
    if (napi_get_value_string_utf8(env, val, NULL, 0, &str_len) != napi_ok || str_len == 0) return NULL;
    char* path = (char*)malloc(str_len + 1);
    if (!path) return NULL;
    size_t copied = 0;
    napi_get_value_string_utf8(env, val, path, str_len + 1, &copied);
    path[copied] = '\0';
    return path;
}

static fs_status_t extract_bytes(napi_env env, napi_value val, const fs_byte_t** out_bytes, fs_size_t* out_len, char** out_allocated) {
    *out_allocated = NULL;
    bool is_buffer = false;
    napi_is_buffer(env, val, &is_buffer);
    if (is_buffer) {
        void* data = NULL;
        size_t length = 0;
        if (napi_get_buffer_info(env, val, &data, &length) != napi_ok) return FS_ERROR_INVALID_ARG;
        *out_bytes = (const fs_byte_t*)data;
        *out_len = (fs_size_t)length;
        return FS_SUCCESS;
    }

    napi_valuetype type;
    if (napi_typeof(env, val, &type) != napi_ok) return FS_ERROR_INVALID_ARG;
    if (type == napi_string) {
        size_t str_len = 0;
        if (napi_get_value_string_utf8(env, val, NULL, 0, &str_len) != napi_ok) return FS_ERROR_INVALID_ARG;
        if (str_len == 0) {
            *out_bytes = (const fs_byte_t*)"";
            *out_len = 0;
            return FS_SUCCESS;
        }
        char* buf = (char*)malloc(str_len + 1);
        if (!buf) return FS_ERROR_OUT_OF_BOUNDS;
        size_t copied = 0;
        napi_get_value_string_utf8(env, val, buf, str_len + 1, &copied);
        buf[copied] = '\0';
        *out_bytes = (const fs_byte_t*)buf;
        *out_len = (fs_size_t)copied;
        *out_allocated = buf;
        return FS_SUCCESS;
    }

    return FS_ERROR_INVALID_ARG;
}

// ==========================================
// ASYNC SCAN DATA & WORKERS
// ==========================================

typedef struct {
    napi_async_work work;
    napi_deferred deferred;
    char* file_path;
    fs_byte_t* pattern_data;
    fs_size_t pattern_len;
    int32_t max_matches;
    fs_size_t* matches;
    fs_size_t match_count;
    fs_status_t scan_status;
} AsyncScanData;

static void ExecuteScan(napi_env env, void* data) {
    (void)env;
    AsyncScanData* async_data = (AsyncScanData*)data;
    fastscan_ctx_t ctx = {0};

    async_data->scan_status = fastscan_init_binary(&ctx, async_data->pattern_data, async_data->pattern_len, (fs_size_t)async_data->max_matches);

    if (async_data->scan_status == FS_SUCCESS) {
        async_data->scan_status = fastscan_load_file(&ctx, async_data->file_path);
        if (async_data->scan_status == FS_SUCCESS) {
            async_data->scan_status = fastscan_execute(&ctx);
        }
    }

    async_data->matches = ctx.matches;
    async_data->match_count = ctx.match_count;
    ctx.matches = NULL;

    fastscan_destroy(&ctx);
}

static void CompleteScan(napi_env env, napi_status status, void* data) {
    AsyncScanData* async_data = (AsyncScanData*)data;

    if (status != napi_ok) {
        napi_value err_msg;
        napi_create_string_utf8(env, "Async internal failure", NAPI_AUTO_LENGTH, &err_msg);
        napi_reject_deferred(env, async_data->deferred, err_msg);
    } else if (async_data->scan_status != FS_SUCCESS) {
        napi_value error_msg;
        const char* err_str = status_to_str(async_data->scan_status);
        napi_create_string_utf8(env, err_str, NAPI_AUTO_LENGTH, &error_msg);
        napi_reject_deferred(env, async_data->deferred, error_msg);
    } else {
        napi_value js_result_array;

        if (async_data->match_count > 0 && async_data->matches) {
            size_t byte_length = async_data->match_count * sizeof(fs_size_t);
            int64_t adjusted = 0;
            napi_adjust_external_memory(env, (int64_t)byte_length, &adjusted);

            napi_value array_buffer;
            napi_create_external_arraybuffer(
                env,
                async_data->matches,
                byte_length,
                FreeMatchesCallback,
                (void*)byte_length,
                &array_buffer
            );

            napi_create_typedarray(
                env,
                napi_biguint64_array,
                async_data->match_count,
                array_buffer,
                0,
                &js_result_array
            );

            async_data->matches = NULL;
        } else {
            napi_create_array_with_length(env, 0, &js_result_array);
        }

        napi_resolve_deferred(env, async_data->deferred, js_result_array);
    }

    if (async_data->file_path) free(async_data->file_path);
    if (async_data->pattern_data) free(async_data->pattern_data);
    if (async_data->matches) free(async_data->matches);

    napi_delete_async_work(env, async_data->work);
    free(async_data);
}

// ==========================================
// CORE SCAN FILE APIS
// ==========================================

static napi_value ScanFileAsync(napi_env env, napi_callback_info info) {
    napi_status status;
    size_t argc = 3;
    napi_value args[3];

    status = napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (status != napi_ok || argc < 3) return throw_error(env, "Invalid arguments. Expected (path, pattern, maxMatches)");

    char* file_path = extract_filepath(env, args[0]);
    if (!file_path) return throw_error(env, "Invalid file path");

    const fs_byte_t* pat_bytes = NULL;
    fs_size_t pat_len = 0;
    char* pat_alloc = NULL;
    fs_status_t extract_st = extract_bytes(env, args[1], &pat_bytes, &pat_len, &pat_alloc);
    if (extract_st != FS_SUCCESS) {
        free(file_path);
        return throw_error(env, "Invalid pattern (must be string or Buffer)");
    }

    int32_t max_matches = 100000;
    status = napi_get_value_int32(env, args[2], &max_matches);
    if (status != napi_ok || max_matches <= 0) {
        free(file_path);
        if (pat_alloc) free(pat_alloc);
        return throw_error(env, "maxMatches must be a positive integer");
    }

    AsyncScanData* async_data = (AsyncScanData*)malloc(sizeof(AsyncScanData));
    if (!async_data) {
        free(file_path);
        if (pat_alloc) free(pat_alloc);
        return throw_error(env, "Memory allocation failed");
    }
    memset(async_data, 0, sizeof(AsyncScanData));

    async_data->file_path = file_path;
    async_data->pattern_len = pat_len;
    if (pat_len > 0) {
        async_data->pattern_data = (fs_byte_t*)malloc(pat_len);
        if (!async_data->pattern_data) {
            free(file_path);
            if (pat_alloc) free(pat_alloc);
            free(async_data);
            return throw_error(env, "Memory allocation failed");
        }
        memcpy(async_data->pattern_data, pat_bytes, pat_len);
    } else {
        async_data->pattern_data = NULL;
    }
    if (pat_alloc) free(pat_alloc);
    async_data->max_matches = max_matches;

    napi_value promise;
    status = napi_create_promise(env, &async_data->deferred, &promise);
    if (status != napi_ok) {
        if (async_data->file_path) free(async_data->file_path);
        if (async_data->pattern_data) free(async_data->pattern_data);
        free(async_data);
        return NULL;
    }

    napi_value resource_name;
    napi_create_string_utf8(env, "fastscan_async_resource", NAPI_AUTO_LENGTH, &resource_name);

    status = napi_create_async_work(
        env,
        NULL,
        resource_name,
        ExecuteScan,
        CompleteScan,
        async_data,
        &async_data->work
    );

    if (status != napi_ok) {
        if (async_data->file_path) free(async_data->file_path);
        if (async_data->pattern_data) free(async_data->pattern_data);
        free(async_data);
        return NULL;
    }

    status = napi_queue_async_work(env, async_data->work);
    if (status != napi_ok) {
        napi_delete_async_work(env, async_data->work);
        if (async_data->file_path) free(async_data->file_path);
        if (async_data->pattern_data) free(async_data->pattern_data);
        free(async_data);
        return NULL;
    }

    return promise;
}

static napi_value ScanFileSync(napi_env env, napi_callback_info info) {
    napi_status status;
    size_t argc = 3;
    napi_value args[3];

    status = napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (status != napi_ok || argc < 3) {
        return throw_error(env, "Invalid arguments. Expected (path, pattern, maxMatches)");
    }

    char* file_path = extract_filepath(env, args[0]);
    if (!file_path) return throw_error(env, "Invalid file path");

    const fs_byte_t* pat_bytes = NULL;
    fs_size_t pat_len = 0;
    char* pat_alloc = NULL;
    fs_status_t extract_st = extract_bytes(env, args[1], &pat_bytes, &pat_len, &pat_alloc);
    if (extract_st != FS_SUCCESS) {
        free(file_path);
        return throw_error(env, "Invalid pattern (must be string or Buffer)");
    }

    int32_t max_matches;
    status = napi_get_value_int32(env, args[2], &max_matches);
    if (status != napi_ok || max_matches <= 0) {
        free(file_path);
        if (pat_alloc) free(pat_alloc);
        return throw_error(env, "maxMatches must be a positive integer");
    }

    fastscan_ctx_t ctx = {0};
    fs_status_t scan_status = fastscan_init_binary(&ctx, pat_bytes, pat_len, (fs_size_t)max_matches);
    if (pat_alloc) free(pat_alloc);

    if (scan_status != FS_SUCCESS) {
        free(file_path);
        return throw_error(env, "Failed to initialize scanner");
    }

    scan_status = fastscan_load_file(&ctx, file_path);
    free(file_path);

    if (scan_status != FS_SUCCESS) {
        fastscan_destroy(&ctx);
        return throw_error(env, status_to_str(scan_status));
    }

    scan_status = fastscan_execute(&ctx);

    napi_value js_result_array = NULL;

    if (scan_status == FS_SUCCESS) {
        if (ctx.match_count > 0 && ctx.matches) {
            size_t byte_length = ctx.match_count * sizeof(fs_size_t);
            int64_t adjusted = 0;
            napi_adjust_external_memory(env, (int64_t)byte_length, &adjusted);

            napi_value array_buffer;
            napi_create_external_arraybuffer(
                env,
                ctx.matches,
                byte_length,
                FreeMatchesCallback,
                (void*)byte_length,
                &array_buffer
            );

            napi_create_typedarray(
                env,
                napi_biguint64_array,
                ctx.match_count,
                array_buffer,
                0,
                &js_result_array
            );

            ctx.matches = NULL;
        } else {
            napi_create_array_with_length(env, 0, &js_result_array);
        }
    } else {
        const char* err_str = status_to_str(scan_status);
        fastscan_destroy(&ctx);
        return throw_error(env, err_str);
    }

    fastscan_destroy(&ctx);
    return js_result_array;
}

// ==========================================
// IN-MEMORY BUFFER SCAN APIS
// ==========================================

static napi_value ScanBufferSync(napi_env env, napi_callback_info info) {
    napi_status status;
    size_t argc = 3;
    napi_value args[3];

    status = napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (status != napi_ok || argc < 3) {
        return throw_error(env, "Invalid arguments. Expected (buffer, pattern, maxMatches)");
    }

    const fs_byte_t* buf_data = NULL;
    fs_size_t buf_len = 0;
    char* buf_alloc = NULL;
    if (extract_bytes(env, args[0], &buf_data, &buf_len, &buf_alloc) != FS_SUCCESS) {
        return throw_error(env, "First argument must be a Buffer or string");
    }

    const fs_byte_t* pat_bytes = NULL;
    fs_size_t pat_len = 0;
    char* pat_alloc = NULL;
    if (extract_bytes(env, args[1], &pat_bytes, &pat_len, &pat_alloc) != FS_SUCCESS) {
        if (buf_alloc) free(buf_alloc);
        return throw_error(env, "Invalid pattern");
    }

    int32_t max_matches = 100000;
    napi_get_value_int32(env, args[2], &max_matches);
    if (max_matches <= 0) max_matches = 100000;

    fastscan_ctx_t ctx = {0};
    fastscan_init_binary(&ctx, pat_bytes, pat_len, (fs_size_t)max_matches);
    if (pat_alloc) free(pat_alloc);

    fastscan_load_buffer(&ctx, buf_data, buf_len);
    fs_status_t scan_status = fastscan_execute(&ctx);

    napi_value js_result_array = NULL;

    if (scan_status == FS_SUCCESS && ctx.match_count > 0 && ctx.matches) {
        size_t byte_length = ctx.match_count * sizeof(fs_size_t);
        int64_t adjusted = 0;
        napi_adjust_external_memory(env, (int64_t)byte_length, &adjusted);

        napi_value array_buffer;
        napi_create_external_arraybuffer(
            env,
            ctx.matches,
            byte_length,
            FreeMatchesCallback,
            (void*)byte_length,
            &array_buffer
        );

        napi_create_typedarray(
            env,
            napi_biguint64_array,
            ctx.match_count,
            array_buffer,
            0,
            &js_result_array
        );
        ctx.matches = NULL;
    } else {
        napi_create_array_with_length(env, 0, &js_result_array);
    }

    fastscan_destroy(&ctx);
    if (buf_alloc) free(buf_alloc);
    return js_result_array;
}

// ==========================================
// CONTEXT SNIPPET EXTRACTOR
// ==========================================

static napi_value ScanWithContextNative(napi_env env, napi_callback_info info) {
    napi_status status;
    size_t argc = 5;
    napi_value args[5];

    status = napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (status != napi_ok || argc < 5) {
        return throw_error(env, "Expected (filepath, pattern, maxMatches, beforeBytes, afterBytes)");
    }

    char* file_path = extract_filepath(env, args[0]);
    if (!file_path) return throw_error(env, "Invalid filepath");

    const fs_byte_t* pat_bytes = NULL;
    fs_size_t pat_len = 0;
    char* pat_alloc = NULL;
    if (extract_bytes(env, args[1], &pat_bytes, &pat_len, &pat_alloc) != FS_SUCCESS) {
        free(file_path);
        return throw_error(env, "Invalid pattern");
    }

    int32_t max_matches, before_bytes, after_bytes;
    napi_get_value_int32(env, args[2], &max_matches);
    napi_get_value_int32(env, args[3], &before_bytes);
    napi_get_value_int32(env, args[4], &after_bytes);

    if (max_matches <= 0 || before_bytes < 0 || after_bytes < 0) {
        free(file_path);
        if (pat_alloc) free(pat_alloc);
        return throw_error(env, "Invalid size parameters");
    }

    fastscan_ctx_t ctx = {0};
    fs_status_t scan_status = fastscan_init_binary(&ctx, pat_bytes, pat_len, (fs_size_t)max_matches);
    if (pat_alloc) free(pat_alloc);

    if (scan_status != FS_SUCCESS) {
        free(file_path);
        return throw_error(env, "Failed to initialize scanner");
    }

    scan_status = fastscan_load_file(&ctx, file_path);
    free(file_path);

    if (scan_status != FS_SUCCESS) {
        const char* err_str = status_to_str(scan_status);
        fastscan_destroy(&ctx);
        return throw_error(env, err_str);
    }

    scan_status = fastscan_execute(&ctx);
    if (scan_status != FS_SUCCESS) {
        const char* err_str = status_to_str(scan_status);
        fastscan_destroy(&ctx);
        return throw_error(env, err_str);
    }

    napi_value result_array;
    napi_create_array_with_length(env, ctx.match_count, &result_array);

    for (fs_size_t i = 0; i < ctx.match_count; i++) {
        fs_size_t offset = ctx.matches[i];
        const char* snippet = NULL;
        fs_size_t snippet_len = 0;

        fs_extract_context(
            ctx.region.data,
            ctx.region.size,
            offset,
            ctx.pattern_len,
            (fs_size_t)before_bytes,
            (fs_size_t)after_bytes,
            &snippet,
            &snippet_len
        );

        napi_value item_obj;
        napi_create_object(env, &item_obj);

        napi_value js_offset;
        napi_create_bigint_uint64(env, offset, &js_offset);
        napi_set_named_property(env, item_obj, "offset", js_offset);

        napi_value js_snippet;
        napi_create_string_utf8(env, snippet ? snippet : "", snippet_len, &js_snippet);
        napi_set_named_property(env, item_obj, "snippet", js_snippet);

        napi_set_element(env, result_array, (uint32_t)i, item_obj);
    }

    fastscan_destroy(&ctx);
    return result_array;
}

// ==========================================
// HIGH-PERFORMANCE LINE & COLUMN INDEXER
// ==========================================

static napi_value ScanWithPositionsNative(napi_env env, napi_callback_info info) {
    napi_status status;
    size_t argc = 5;
    napi_value args[5];

    status = napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (status != napi_ok || argc < 5) {
        return throw_error(env, "Expected (filepath, pattern, maxMatches, beforeBytes, afterBytes)");
    }

    char* file_path = extract_filepath(env, args[0]);
    if (!file_path) return throw_error(env, "Invalid filepath");

    const fs_byte_t* pat_bytes = NULL;
    fs_size_t pat_len = 0;
    char* pat_alloc = NULL;
    if (extract_bytes(env, args[1], &pat_bytes, &pat_len, &pat_alloc) != FS_SUCCESS) {
        free(file_path);
        return throw_error(env, "Invalid pattern");
    }

    int32_t max_matches, before_bytes, after_bytes;
    napi_get_value_int32(env, args[2], &max_matches);
    napi_get_value_int32(env, args[3], &before_bytes);
    napi_get_value_int32(env, args[4], &after_bytes);

    if (max_matches <= 0 || before_bytes < 0 || after_bytes < 0) {
        free(file_path);
        if (pat_alloc) free(pat_alloc);
        return throw_error(env, "Invalid size parameters");
    }

    fastscan_ctx_t ctx = {0};
    if (fastscan_init_binary(&ctx, pat_bytes, pat_len, (fs_size_t)max_matches) != FS_SUCCESS) {
        free(file_path);
        if (pat_alloc) free(pat_alloc);
        return throw_error(env, "Init failed");
    }
    if (pat_alloc) free(pat_alloc);

    fs_status_t load_st = fastscan_load_file(&ctx, file_path);
    free(file_path);
    if (load_st != FS_SUCCESS) {
        fastscan_destroy(&ctx);
        return throw_error(env, status_to_str(load_st));
    }

    if (fastscan_execute(&ctx) != FS_SUCCESS) {
        fastscan_destroy(&ctx);
        return throw_error(env, "Exec failed");
    }

    napi_value result_array;
    napi_create_array_with_length(env, ctx.match_count, &result_array);

    if (ctx.match_count > 0 && ctx.matches) {
        fs_size_t* lines = (fs_size_t*)malloc(ctx.match_count * sizeof(fs_size_t));
        fs_size_t* cols = (fs_size_t*)malloc(ctx.match_count * sizeof(fs_size_t));

        if (lines && cols) {
            // Linear single-pass cumulative line and column calculation O(N)
            fs_calculate_line_col_batch(ctx.region.data, ctx.region.size, ctx.matches, ctx.match_count, lines, cols);

            for (fs_size_t i = 0; i < ctx.match_count; i++) {
                fs_size_t offset = ctx.matches[i];
                const char* snippet = NULL;
                fs_size_t snippet_len = 0;
                fs_extract_context(ctx.region.data, ctx.region.size, offset, ctx.pattern_len, (fs_size_t)before_bytes, (fs_size_t)after_bytes, &snippet, &snippet_len);

                napi_value item_obj;
                napi_create_object(env, &item_obj);

                napi_value js_offset, js_line, js_col, js_snippet;
                napi_create_bigint_uint64(env, offset, &js_offset);
                napi_create_uint32(env, (uint32_t)lines[i], &js_line);
                napi_create_uint32(env, (uint32_t)cols[i], &js_col);
                napi_create_string_utf8(env, snippet ? snippet : "", snippet_len, &js_snippet);

                napi_set_named_property(env, item_obj, "offset", js_offset);
                napi_set_named_property(env, item_obj, "line", js_line);
                napi_set_named_property(env, item_obj, "column", js_col);
                napi_set_named_property(env, item_obj, "snippet", js_snippet);

                napi_set_element(env, result_array, (uint32_t)i, item_obj);
            }
            free(lines);
            free(cols);
        } else {
            if (lines) free(lines);
            if (cols) free(cols);
        }
    }

    fastscan_destroy(&ctx);
    return result_array;
}

// ==========================================
// MULTI-PATTERN SCANNER APIS
// ==========================================

static napi_value ScanMultiSync(napi_env env, napi_callback_info info) {
    napi_status status;
    size_t argc = 3;
    napi_value args[3];

    status = napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (status != napi_ok || argc < 3) return throw_error(env, "Expected (filepath, patternsArray, maxMatches)");

    char* file_path = extract_filepath(env, args[0]);
    if (!file_path) return throw_error(env, "Invalid file path");

    uint32_t pattern_count = 0;
    napi_get_array_length(env, args[1], &pattern_count);
    if (pattern_count == 0) {
        free(file_path);
        return throw_error(env, "Patterns array cannot be empty");
    }

    int32_t max_matches = 100000;
    napi_get_value_int32(env, args[2], &max_matches);
    if (max_matches <= 0) max_matches = 100000;

    char** patterns = (char**)malloc(pattern_count * sizeof(char*));
    fs_size_t* pattern_lens = (fs_size_t*)malloc(pattern_count * sizeof(fs_size_t));
    if (!patterns || !pattern_lens) {
        free(file_path);
        if (patterns) free(patterns);
        if (pattern_lens) free(pattern_lens);
        return throw_error(env, "Memory allocation failed");
    }

    for (uint32_t i = 0; i < pattern_count; i++) {
        napi_value item;
        napi_get_element(env, args[1], i, &item);
        size_t len = 0;
        napi_get_value_string_utf8(env, item, NULL, 0, &len);
        patterns[i] = (char*)malloc(len + 1);
        if (patterns[i]) {
            size_t copied = 0;
            napi_get_value_string_utf8(env, item, patterns[i], len + 1, &copied);
            patterns[i][copied] = '\0';
            pattern_lens[i] = copied;
        } else {
            pattern_lens[i] = 0;
        }
    }

    fs_region_t region = {0};
    fs_status_t map_status = fs_mmap_open(file_path, &region);
    free(file_path);

    if (map_status != FS_SUCCESS) {
        for (uint32_t i = 0; i < pattern_count; i++) { if (patterns[i]) free(patterns[i]); }
        free(patterns); free(pattern_lens);
        return throw_error(env, status_to_str(map_status));
    }

    fs_multi_match_t* out_matches = (fs_multi_match_t*)malloc(max_matches * sizeof(fs_multi_match_t));
    if (!out_matches) {
        fs_mmap_close(&region);
        for (uint32_t i = 0; i < pattern_count; i++) { if (patterns[i]) free(patterns[i]); }
        free(patterns); free(pattern_lens);
        return throw_error(env, "Memory allocation failed");
    }

    fs_size_t match_count = 0;
    fs_cpu_features_t cpu = fs_detect_cpu_features();

    fs_scan_multi_raw(region.data, region.size, (const char**)patterns, pattern_lens, pattern_count, out_matches, &match_count, max_matches, &cpu);

    napi_value result_array;
    napi_create_array_with_length(env, match_count, &result_array);

    for (fs_size_t i = 0; i < match_count; i++) {
        napi_value item_obj;
        napi_create_object(env, &item_obj);

        napi_value js_pat_idx, js_pat_str, js_offset;
        napi_create_uint32(env, (uint32_t)out_matches[i].pattern_index, &js_pat_idx);
        napi_create_string_utf8(env, patterns[out_matches[i].pattern_index], pattern_lens[out_matches[i].pattern_index], &js_pat_str);
        napi_create_bigint_uint64(env, out_matches[i].offset, &js_offset);

        napi_set_named_property(env, item_obj, "patternIndex", js_pat_idx);
        napi_set_named_property(env, item_obj, "pattern", js_pat_str);
        napi_set_named_property(env, item_obj, "offset", js_offset);

        napi_set_element(env, result_array, (uint32_t)i, item_obj);
    }

    free(out_matches);
    fs_mmap_close(&region);
    for (uint32_t i = 0; i < pattern_count; i++) { if (patterns[i]) free(patterns[i]); }
    free(patterns); free(pattern_lens);

    return result_array;
}

// Real non-blocking Async Multi-Pattern Worker
typedef struct {
    napi_async_work work;
    napi_deferred deferred;
    char* file_path;
    char** patterns;
    fs_size_t* pattern_lens;
    uint32_t pattern_count;
    int32_t max_matches;
    fs_multi_match_t* out_matches;
    fs_size_t match_count;
    fs_status_t scan_status;
} AsyncMultiScanData;

static void ExecuteMultiScan(napi_env env, void* data) {
    (void)env;
    AsyncMultiScanData* d = (AsyncMultiScanData*)data;
    fs_region_t region = {0};

    d->scan_status = fs_mmap_open(d->file_path, &region);
    if (d->scan_status != FS_SUCCESS) return;

    d->out_matches = (fs_multi_match_t*)malloc(d->max_matches * sizeof(fs_multi_match_t));
    if (!d->out_matches) {
        fs_mmap_close(&region);
        d->scan_status = FS_ERROR_OUT_OF_BOUNDS;
        return;
    }

    fs_cpu_features_t cpu = fs_detect_cpu_features();
    d->scan_status = fs_scan_multi_raw(
        region.data,
        region.size,
        (const char**)d->patterns,
        d->pattern_lens,
        d->pattern_count,
        d->out_matches,
        &d->match_count,
        d->max_matches,
        &cpu
    );

    fs_mmap_close(&region);
}

static void CompleteMultiScan(napi_env env, napi_status status, void* data) {
    AsyncMultiScanData* d = (AsyncMultiScanData*)data;

    if (status != napi_ok || d->scan_status != FS_SUCCESS) {
        napi_value err_msg;
        const char* s = status_to_str(d->scan_status);
        napi_create_string_utf8(env, s, NAPI_AUTO_LENGTH, &err_msg);
        napi_reject_deferred(env, d->deferred, err_msg);
    } else {
        napi_value result_array;
        napi_create_array_with_length(env, d->match_count, &result_array);

        for (fs_size_t i = 0; i < d->match_count; i++) {
            napi_value item_obj;
            napi_create_object(env, &item_obj);

            napi_value js_pat_idx, js_pat_str, js_offset;
            napi_create_uint32(env, (uint32_t)d->out_matches[i].pattern_index, &js_pat_idx);
            napi_create_string_utf8(env, d->patterns[d->out_matches[i].pattern_index], d->pattern_lens[d->out_matches[i].pattern_index], &js_pat_str);
            napi_create_bigint_uint64(env, d->out_matches[i].offset, &js_offset);

            napi_set_named_property(env, item_obj, "patternIndex", js_pat_idx);
            napi_set_named_property(env, item_obj, "pattern", js_pat_str);
            napi_set_named_property(env, item_obj, "offset", js_offset);

            napi_set_element(env, result_array, (uint32_t)i, item_obj);
        }

        napi_resolve_deferred(env, d->deferred, result_array);
    }

    if (d->file_path) free(d->file_path);
    if (d->patterns) {
        for (uint32_t i = 0; i < d->pattern_count; i++) { if (d->patterns[i]) free(d->patterns[i]); }
        free(d->patterns);
    }
    if (d->pattern_lens) free(d->pattern_lens);
    if (d->out_matches) free(d->out_matches);

    napi_delete_async_work(env, d->work);
    free(d);
}

static napi_value ScanMultiAsync(napi_env env, napi_callback_info info) {
    napi_status status;
    size_t argc = 3;
    napi_value args[3];

    status = napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (status != napi_ok || argc < 3) return throw_error(env, "Expected (filepath, patternsArray, maxMatches)");

    char* file_path = extract_filepath(env, args[0]);
    if (!file_path) return throw_error(env, "Invalid file path");

    uint32_t pattern_count = 0;
    napi_get_array_length(env, args[1], &pattern_count);
    if (pattern_count == 0) {
        free(file_path);
        return throw_error(env, "Patterns array cannot be empty");
    }

    int32_t max_matches = 100000;
    napi_get_value_int32(env, args[2], &max_matches);
    if (max_matches <= 0) max_matches = 100000;

    AsyncMultiScanData* d = (AsyncMultiScanData*)malloc(sizeof(AsyncMultiScanData));
    if (!d) {
        free(file_path);
        return throw_error(env, "Memory allocation failed");
    }
    memset(d, 0, sizeof(AsyncMultiScanData));

    d->file_path = file_path;
    d->pattern_count = pattern_count;
    d->max_matches = max_matches;

    d->patterns = (char**)malloc(pattern_count * sizeof(char*));
    d->pattern_lens = (fs_size_t*)malloc(pattern_count * sizeof(fs_size_t));
    if (!d->patterns || !d->pattern_lens) {
        if (d->patterns) free(d->patterns);
        if (d->pattern_lens) free(d->pattern_lens);
        free(file_path);
        free(d);
        return throw_error(env, "Memory allocation failed");
    }

    for (uint32_t i = 0; i < pattern_count; i++) {
        napi_value item;
        napi_get_element(env, args[1], i, &item);
        size_t len = 0;
        napi_get_value_string_utf8(env, item, NULL, 0, &len);
        d->patterns[i] = (char*)malloc(len + 1);
        if (d->patterns[i]) {
            size_t copied = 0;
            napi_get_value_string_utf8(env, item, d->patterns[i], len + 1, &copied);
            d->patterns[i][copied] = '\0';
            d->pattern_lens[i] = copied;
        } else {
            d->pattern_lens[i] = 0;
        }
    }

    napi_value promise;
    status = napi_create_promise(env, &d->deferred, &promise);
    if (status != napi_ok) {
        free(d->file_path);
        for (uint32_t i = 0; i < pattern_count; i++) { if (d->patterns[i]) free(d->patterns[i]); }
        free(d->patterns);
        free(d->pattern_lens);
        free(d);
        return NULL;
    }

    napi_value res_name;
    napi_create_string_utf8(env, "fastscan_multi_async_work", NAPI_AUTO_LENGTH, &res_name);

    status = napi_create_async_work(env, NULL, res_name, ExecuteMultiScan, CompleteMultiScan, d, &d->work);
    if (status != napi_ok) {
        free(d->file_path);
        for (uint32_t i = 0; i < pattern_count; i++) { if (d->patterns[i]) free(d->patterns[i]); }
        free(d->patterns);
        free(d->pattern_lens);
        free(d);
        return NULL;
    }

    napi_queue_async_work(env, d->work);
    return promise;
}

// ==========================================
// HARDWARE FEATURE INTROSPECTION & CLEANUP
// ==========================================

static napi_value GetCpuFeatures(napi_env env, napi_callback_info info) {
    (void)info;
    fs_cpu_features_t cpu = fs_detect_cpu_features();
    napi_value obj;
    napi_create_object(env, &obj);

    napi_value sse2, avx2, avx512, neon;
    napi_get_boolean(env, cpu.has_sse2, &sse2);
    napi_get_boolean(env, cpu.has_avx2, &avx2);
    napi_get_boolean(env, cpu.has_avx512, &avx512);
    napi_get_boolean(env, cpu.has_neon, &neon);

    napi_set_named_property(env, obj, "sse2", sse2);
    napi_set_named_property(env, obj, "avx2", avx2);
    napi_set_named_property(env, obj, "avx512", avx512);
    napi_set_named_property(env, obj, "neon", neon);

    return obj;
}

static void CleanupHook(void* arg) {
    (void)arg;
    fs_thread_pool_shutdown();
}

static napi_value Init(napi_env env, napi_value exports) {
    napi_status status;
    napi_value fn;

    // Register cleanup hook to gracefully shutdown persistent threads on Node exit
    napi_add_env_cleanup_hook(env, CleanupHook, NULL);

    status = napi_create_function(env, NULL, 0, ScanFileSync, NULL, &fn);
    if (status != napi_ok) return NULL;
    napi_set_named_property(env, exports, "scanFile", fn);

    status = napi_create_function(env, NULL, 0, ScanFileAsync, NULL, &fn);
    if (status != napi_ok) return NULL;
    napi_set_named_property(env, exports, "scanFileAsync", fn);

    status = napi_create_function(env, NULL, 0, ScanBufferSync, NULL, &fn);
    if (status != napi_ok) return NULL;
    napi_set_named_property(env, exports, "scanBuffer", fn);

    status = napi_create_function(env, NULL, 0, ScanWithContextNative, NULL, &fn);
    if (status != napi_ok) return NULL;
    napi_set_named_property(env, exports, "scanWithContextNative", fn);

    status = napi_create_function(env, NULL, 0, ScanWithPositionsNative, NULL, &fn);
    if (status != napi_ok) return NULL;
    napi_set_named_property(env, exports, "scanWithPositionsNative", fn);

    status = napi_create_function(env, NULL, 0, ScanMultiSync, NULL, &fn);
    if (status != napi_ok) return NULL;
    napi_set_named_property(env, exports, "scanFileMulti", fn);

    status = napi_create_function(env, NULL, 0, ScanMultiAsync, NULL, &fn);
    if (status != napi_ok) return NULL;
    napi_set_named_property(env, exports, "scanFileMultiAsync", fn);

    status = napi_create_function(env, NULL, 0, GetCpuFeatures, NULL, &fn);
    if (status != napi_ok) return NULL;
    napi_set_named_property(env, exports, "getCpuFeatures", fn);

    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
