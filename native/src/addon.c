#include <node_api.h>
#include <stdlib.h>
#include <string.h>
#include "../include/fastscan.h"
#include "../include/mmap_reader.h"

static napi_value throw_error(napi_env env, const char* msg) {
    napi_throw_error(env, NULL, msg);
    return NULL;
}

static void FreeMatchesCallback(napi_env env, void* data, void* hint) {
    free(data);
}

typedef struct {
    napi_async_work work;
    napi_deferred deferred;
    char file_path[1024];
    char pattern[4096];
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

    fs_mmap_close(&ctx.region);
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
        if (async_data->scan_status == FS_ERROR_MMAP_FAILED) err_str = "Memory mapping failed";
        if (async_data->scan_status == FS_ERROR_OUT_OF_BOUNDS) err_str = "Buffer allocation failed";
        if (async_data->scan_status == FS_ERROR_INVALID_ARG) err_str = "Invalid argument";

        napi_create_string_utf8(env, err_str, NAPI_AUTO_LENGTH, &error_msg);
        napi_reject_deferred(env, async_data->deferred, error_msg);
    } else {
        napi_value js_result_array;
        
        if (async_data->match_count > 0) {
            size_t byte_length = async_data->match_count * sizeof(fs_size_t);

            napi_value array_buffer;
            napi_create_external_arraybuffer(
                env,
                async_data->matches,
                byte_length,
                FreeMatchesCallback,
                NULL,
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
    napi_create_string_utf8(env, "fastscan_resource", NAPI_AUTO_LENGTH, &resource_name);

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
    char file_path[1024];
    char pattern[4096];
    
    status = napi_get_value_string_utf8(env, args[0], file_path, sizeof(file_path), &path_len);
    if (status != napi_ok) return throw_error(env, "Invalid file path");
    if (path_len >= sizeof(file_path)) return throw_error(env, "File path too long");
    
    status = napi_get_value_string_utf8(env, args[1], pattern, sizeof(pattern), &pattern_len);
    if (status != napi_ok) return throw_error(env, "Invalid pattern");
    if (pattern_len >= sizeof(pattern)) return throw_error(env, "Pattern too long");

    int32_t max_matches;
    status = napi_get_value_int32(env, args[2], &max_matches);
    if (status != napi_ok) return throw_error(env, "Invalid maxMatches value");
    if (max_matches <= 0) return throw_error(env, "maxMatches must be positive");

    fastscan_ctx_t ctx = {0};
    fs_status_t scan_status = fastscan_init(&ctx, pattern, (fs_size_t)max_matches);

    if (scan_status != FS_SUCCESS) {
        return throw_error(env, "Failed to initialize scanner");
    }

    scan_status = fastscan_load_file(&ctx, file_path);
    if (scan_status != FS_SUCCESS) {
        fastscan_destroy(&ctx);
        if (scan_status == FS_ERROR_OPEN_FAILED) {
            return throw_error(env, "Failed to open file");
        }
        return throw_error(env, "Failed to map file to memory");
    }

    scan_status = fastscan_execute(&ctx);
    
    napi_value js_result_array = NULL;
    
    if (scan_status == FS_SUCCESS) {
        if (ctx.match_count > 0) {
            size_t byte_length = ctx.match_count * sizeof(fs_size_t);

            napi_value array_buffer;
            napi_create_external_arraybuffer(
                env, 
                ctx.matches, 
                byte_length, 
                FreeMatchesCallback, 
                NULL, 
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

static napi_value Init(napi_env env, napi_value exports) {
    napi_status status;
    napi_value fn;

    status = napi_create_function(env, NULL, 0, ScanFileSync, NULL, &fn);
    if (status != napi_ok) return NULL;
    napi_set_named_property(env, exports, "scanFile", fn);

    status = napi_create_function(env, NULL, 0, ScanFileAsync, NULL, &fn);
    if (status != napi_ok) return NULL;
    napi_set_named_property(env, exports, "scanFileAsync", fn);

    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
