#include <node_api.h>
#include <stdlib.h>
#include <string.h>
#include "../include/fastscan.h"
#include "../include/mmap_reader.h"
#include "../include/scanner.h"
#include "../include/platform.h"
#include "../include/thread_pool.h"

static napi_value throw_error(napi_env env, const char* msg) {
    napi_throw_error(env, NULL, msg);
    return NULL;
}

static void FreeMatchesCallback(napi_env env, void* data, void* hint) {
    size_t byte_length = (size_t)hint;
    if (byte_length > 0) {
        int64_t adjusted = 0;
        napi_adjust_external_memory(env, -(int64_t)byte_length, &adjusted);
    }
    free(data);
}

typedef struct {
    napi_async_work work;
    napi_deferred deferred;
    char file_path[4096];
    char pattern[8192];
    int32_t max_matches;
    fs_size_t* matches;
    fs_size_t match_count;
    fs_status_t scan_status;
} AsyncScanData;

static void ExecuteScan(napi_env env, void* data) {
    AsyncScanData* async_data = (AsyncScanData*)data;
    fastscan_ctx_t ctx = {0};

    async_data->scan_status = fastscan_init(&ctx, async_data->pattern, (fs_size_t)async_data->max_matches);

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
        const char* err_str = "Unknown Error";
        if (async_data->scan_status == FS_ERROR_OPEN_FAILED) err_str = "File not found";
        else if (async_data->scan_status == FS_ERROR_MMAP_FAILED) err_str = "Memory mapping failed";
        else if (async_data->scan_status == FS_ERROR_OUT_OF_BOUNDS) err_str = "Buffer allocation failed";
        else if (async_data->scan_status == FS_ERROR_INVALID_ARG) err_str = "Invalid argument";
        else if (async_data->scan_status == FS_ERROR_FILE_TRUNCATED || async_data->scan_status == FS_ERROR_BUS_FAULT) err_str = "File truncated during scan";

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

    if (async_data->matches) {
        free(async_data->matches);
    }

    napi_delete_async_work(env, async_data->work);
    free(async_data);
}

static napi_value ScanFileAsync(napi_env env, napi_callback_info info) {
    napi_status status;
    size_t argc = 3;
    napi_value args[3];

    status = napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (status != napi_ok || argc < 3) return throw_error(env, "Invalid arguments. Expected (path, pattern, maxMatches)");

    AsyncScanData* async_data = (AsyncScanData*)malloc(sizeof(AsyncScanData));
    if (!async_data) return throw_error(env, "Memory allocation failed");
    memset(async_data, 0, sizeof(AsyncScanData));

    size_t len;
    status = napi_get_value_string_utf8(env, args[0], async_data->file_path, sizeof(async_data->file_path), &len);
    if (status != napi_ok) { free(async_data); return throw_error(env, "Invalid file path"); }
    if (len >= sizeof(async_data->file_path)) { free(async_data); return throw_error(env, "File path too long"); }

    status = napi_get_value_string_utf8(env, args[1], async_data->pattern, sizeof(async_data->pattern), &len);
    if (status != napi_ok) { free(async_data); return throw_error(env, "Invalid pattern"); }
    if (len >= sizeof(async_data->pattern)) { free(async_data); return throw_error(env, "Pattern too long"); }

    status = napi_get_value_int32(env, args[2], &async_data->max_matches);
    if (status != napi_ok) { free(async_data); return throw_error(env, "Invalid maxMatches"); }
    if (async_data->max_matches <= 0) { free(async_data); return throw_error(env, "maxMatches must be positive"); }

    napi_value promise;
    status = napi_create_promise(env, &async_data->deferred, &promise);
    if (status != napi_ok) { free(async_data); return NULL; }

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
        free(async_data);
        return NULL;
    }

    status = napi_queue_async_work(env, async_data->work);
    if (status != napi_ok) {
        napi_delete_async_work(env, async_data->work);
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

    size_t path_len, pattern_len;
    char file_path[4096];
    char pattern[8192];

    status = napi_get_value_string_utf8(env, args[0], file_path, sizeof(file_path), &path_len);
    if (status != napi_ok || path_len >= sizeof(file_path)) return throw_error(env, "Invalid file path");

    status = napi_get_value_string_utf8(env, args[1], pattern, sizeof(pattern), &pattern_len);
    if (status != napi_ok || pattern_len >= sizeof(pattern)) return throw_error(env, "Invalid pattern");

    int32_t max_matches;
    status = napi_get_value_int32(env, args[2], &max_matches);
    if (status != napi_ok || max_matches <= 0) return throw_error(env, "maxMatches must be a positive integer");

    fastscan_ctx_t ctx = {0};
    fs_status_t scan_status = fastscan_init(&ctx, pattern, (fs_size_t)max_matches);
    if (scan_status != FS_SUCCESS) return throw_error(env, "Failed to initialize scanner");

    scan_status = fastscan_load_file(&ctx, file_path);
    if (scan_status != FS_SUCCESS) {
        fastscan_destroy(&ctx);
        if (scan_status == FS_ERROR_OPEN_FAILED) return throw_error(env, "File not found");
        return throw_error(env, "Memory mapping failed");
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
        fastscan_destroy(&ctx);
        return throw_error(env, "Error during scanning process");
    }

    fastscan_destroy(&ctx);
    return js_result_array;
}

// Native Zero-Syscall Context Snippet Extractor
static napi_value ScanWithContextNative(napi_env env, napi_callback_info info) {
    napi_status status;
    size_t argc = 5;
    napi_value args[5];

    status = napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (status != napi_ok || argc < 5) {
        return throw_error(env, "Expected (filepath, pattern, maxMatches, beforeBytes, afterBytes)");
    }

    size_t path_len, pattern_len;
    char file_path[4096];
    char pattern[8192];

    status = napi_get_value_string_utf8(env, args[0], file_path, sizeof(file_path), &path_len);
    if (status != napi_ok) return throw_error(env, "Invalid filepath");

    status = napi_get_value_string_utf8(env, args[1], pattern, sizeof(pattern), &pattern_len);
    if (status != napi_ok) return throw_error(env, "Invalid pattern");

    int32_t max_matches, before_bytes, after_bytes;
    napi_get_value_int32(env, args[2], &max_matches);
    napi_get_value_int32(env, args[3], &before_bytes);
    napi_get_value_int32(env, args[4], &after_bytes);

    if (max_matches <= 0 || before_bytes < 0 || after_bytes < 0) {
        return throw_error(env, "Invalid size parameters");
    }

    fastscan_ctx_t ctx = {0};
    fs_status_t scan_status = fastscan_init(&ctx, pattern, (fs_size_t)max_matches);
    if (scan_status != FS_SUCCESS) return throw_error(env, "Failed to initialize scanner");

    scan_status = fastscan_load_file(&ctx, file_path);
    if (scan_status != FS_SUCCESS) {
        fastscan_destroy(&ctx);
        return throw_error(env, "Failed to open or map file");
    }

    scan_status = fastscan_execute(&ctx);
    if (scan_status != FS_SUCCESS) {
        fastscan_destroy(&ctx);
        return throw_error(env, "Scan execution failed");
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
        napi_create_string_utf8(env, snippet, snippet_len, &js_snippet);
        napi_set_named_property(env, item_obj, "snippet", js_snippet);

        napi_set_element(env, result_array, (uint32_t)i, item_obj);
    }

    fastscan_destroy(&ctx);
    return result_array;
}

// Vectorized Line and Column Indexer Native API
static napi_value ScanWithPositionsNative(napi_env env, napi_callback_info info) {
    napi_status status;
    size_t argc = 5;
    napi_value args[5];

    status = napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (status != napi_ok || argc < 5) {
        return throw_error(env, "Expected (filepath, pattern, maxMatches, beforeBytes, afterBytes)");
    }

    size_t path_len, pattern_len;
    char file_path[4096];
    char pattern[8192];

    napi_get_value_string_utf8(env, args[0], file_path, sizeof(file_path), &path_len);
    napi_get_value_string_utf8(env, args[1], pattern, sizeof(pattern), &pattern_len);

    int32_t max_matches, before_bytes, after_bytes;
    napi_get_value_int32(env, args[2], &max_matches);
    napi_get_value_int32(env, args[3], &before_bytes);
    napi_get_value_int32(env, args[4], &after_bytes);

    fastscan_ctx_t ctx = {0};
    if (fastscan_init(&ctx, pattern, (fs_size_t)max_matches) != FS_SUCCESS) return throw_error(env, "Init failed");
    if (fastscan_load_file(&ctx, file_path) != FS_SUCCESS) { fastscan_destroy(&ctx); return throw_error(env, "Map failed"); }
    if (fastscan_execute(&ctx) != FS_SUCCESS) { fastscan_destroy(&ctx); return throw_error(env, "Exec failed"); }

    napi_value result_array;
    napi_create_array_with_length(env, ctx.match_count, &result_array);

    for (fs_size_t i = 0; i < ctx.match_count; i++) {
        fs_size_t offset = ctx.matches[i];
        fs_size_t line = 1, col = 1;
        fs_calculate_line_col(ctx.region.data, ctx.region.size, offset, &line, &col);

        const char* snippet = NULL;
        fs_size_t snippet_len = 0;
        fs_extract_context(ctx.region.data, ctx.region.size, offset, ctx.pattern_len, (fs_size_t)before_bytes, (fs_size_t)after_bytes, &snippet, &snippet_len);

        napi_value item_obj;
        napi_create_object(env, &item_obj);

        napi_value js_offset, js_line, js_col, js_snippet;
        napi_create_bigint_uint64(env, offset, &js_offset);
        napi_create_uint32(env, (uint32_t)line, &js_line);
        napi_create_uint32(env, (uint32_t)col, &js_col);
        napi_create_string_utf8(env, snippet, snippet_len, &js_snippet);

        napi_set_named_property(env, item_obj, "offset", js_offset);
        napi_set_named_property(env, item_obj, "line", js_line);
        napi_set_named_property(env, item_obj, "column", js_col);
        napi_set_named_property(env, item_obj, "snippet", js_snippet);

        napi_set_element(env, result_array, (uint32_t)i, item_obj);
    }

    fastscan_destroy(&ctx);
    return result_array;
}

// Multi-Pattern Native Scanner API
static napi_value ScanMultiSync(napi_env env, napi_callback_info info) {
    napi_status status;
    size_t argc = 3;
    napi_value args[3];

    status = napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (status != napi_ok || argc < 3) return throw_error(env, "Expected (filepath, patternsArray, maxMatches)");

    char file_path[4096];
    size_t path_len;
    napi_get_value_string_utf8(env, args[0], file_path, sizeof(file_path), &path_len);

    uint32_t pattern_count = 0;
    napi_get_array_length(env, args[1], &pattern_count);
    if (pattern_count == 0) return throw_error(env, "Patterns array cannot be empty");

    int32_t max_matches = 100000;
    napi_get_value_int32(env, args[2], &max_matches);

    char** patterns = (char**)malloc(pattern_count * sizeof(char*));
    fs_size_t* pattern_lens = (fs_size_t*)malloc(pattern_count * sizeof(fs_size_t));

    for (uint32_t i = 0; i < pattern_count; i++) {
        napi_value item;
        napi_get_element(env, args[1], i, &item);
        patterns[i] = (char*)malloc(1024);
        size_t len = 0;
        napi_get_value_string_utf8(env, item, patterns[i], 1024, &len);
        pattern_lens[i] = len;
    }

    fs_region_t region = {0};
    fs_status_t map_status = fs_mmap_open(file_path, &region);
    if (map_status != FS_SUCCESS) {
        for (uint32_t i = 0; i < pattern_count; i++) free(patterns[i]);
        free(patterns); free(pattern_lens);
        return throw_error(env, "Failed to open or map file");
    }

    fs_multi_match_t* out_matches = (fs_multi_match_t*)malloc(max_matches * sizeof(fs_multi_match_t));
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
    for (uint32_t i = 0; i < pattern_count; i++) free(patterns[i]);
    free(patterns); free(pattern_lens);

    return result_array;
}

// Get CPU capabilities
static napi_value GetCpuFeatures(napi_env env, napi_callback_info info) {
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

static napi_value Init(napi_env env, napi_value exports) {
    napi_status status;
    napi_value fn;

    // Initialize warm persistent worker pool
    fs_thread_pool_get_global();

    status = napi_create_function(env, NULL, 0, ScanFileSync, NULL, &fn);
    if (status != napi_ok) return NULL;
    napi_set_named_property(env, exports, "scanFile", fn);

    status = napi_create_function(env, NULL, 0, ScanFileAsync, NULL, &fn);
    if (status != napi_ok) return NULL;
    napi_set_named_property(env, exports, "scanFileAsync", fn);

    status = napi_create_function(env, NULL, 0, ScanWithContextNative, NULL, &fn);
    if (status != napi_ok) return NULL;
    napi_set_named_property(env, exports, "scanWithContextNative", fn);

    status = napi_create_function(env, NULL, 0, ScanWithPositionsNative, NULL, &fn);
    if (status != napi_ok) return NULL;
    napi_set_named_property(env, exports, "scanWithPositionsNative", fn);

    status = napi_create_function(env, NULL, 0, ScanMultiSync, NULL, &fn);
    if (status != napi_ok) return NULL;
    napi_set_named_property(env, exports, "scanFileMulti", fn);

    status = napi_create_function(env, NULL, 0, GetCpuFeatures, NULL, &fn);
    if (status != napi_ok) return NULL;
    napi_set_named_property(env, exports, "getCpuFeatures", fn);

    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
