/*
 * controller.c
 */

#include "utils.h"
#include "controller.h"

/**
 * Root value of the dudley JSON file
 */
static struct json_value_t *_json_root_value = NULL;

/**
 * Raw data buffer consumed from JSON "input" method
 */
static uint8_t *_raw_data = NULL;

/**
 * Size of raw data buffer
 */
static size_t _raw_data_size = 0;

/**
 * Output parameters
 */
static struct output_params *_output_params;

/**
 * Read the inline hex string from the file and write it to _raw_data as a bytearray
 */
static int _consume_inline_data(struct json_value_t *json_input_value)
{
    size_t data_size = 0;
    size_t i;
    const char *pos = NULL;
    const char *hexstr = json_object_get_string(json_object(json_input_value), "data");
    if (!hexstr) {
        duderr("input data was not supplied");
        return FAILURE;
    }

    // Ensure the input is a hex string
    if (hexstr[strspn(hexstr, "0123456789abcdefABCDEF")]) {
        duderr("input data is not a valid hex string");
        return FAILURE;
    }

    // Allocate the raw data buffer
    data_size = strlen(hexstr) / 2;
    _raw_data = (uint8_t *)malloc(data_size);
    if (!_raw_data) {
        duderr("out of memory");
        return FAILURE;
    }

    // Convert the hex string to a byte array and copy
    pos = hexstr;
    for (i = 0; i < data_size; i++) {
        sscanf(pos, "%2hhx", &_raw_data[i]);
        pos += 2;
        _raw_data_size++;
    }

    return SUCCESS;
}

/**
 * Parse input value and set data based on parameters
 */
static int _set_input_params(void)
{
    struct json_value_t *json_input_value = NULL;
    const char *method = NULL;
    int ret = FAILURE;

    if (_json_root_value == NULL) {
        goto done;
    }

    json_input_value = json_object_get_value(json_object(_json_root_value), "input");
    if (!json_input_value) {
        goto done;
    }

    method = json_object_get_string(json_object(json_input_value), "method");
    if (!method) {
        duderr("input method was not specified");
        goto done;
    }

    if (!strcmp(method, "inline-data")) {
        if (_consume_inline_data(json_input_value)) {
            goto done;
        }

        dudinfo("%lu bytes of input data consumed", _raw_data_size);
    } else {
        duderr("unsupported input method: %s", method);
        goto done;
    }
    ret = SUCCESS;
done:
    if (json_input_value) {
        json_value_free(json_input_value);
    }

    return ret;
}

/**
 * Parse file output parameters and set _output_params to use them
 */
static int _set_file_out_params(struct json_value_t *json_output_value)
{
    const char *directory_path = NULL;
    const char *name_suffix = NULL;
    struct file_out_params *fout_params = NULL;

    directory_path = json_object_get_string(json_object(json_output_value), "directory-path");
    if (!directory_path) {
        duderr("Export directory path not supplied: \"directory-path\"");
        return FAILURE;
    }

    name_suffix = json_object_get_string(json_object(json_output_value), "name-suffix");
    if (!name_suffix) {
        duderr("Name suffix for exported files not supplied: \"name-suffix\"");
        return FAILURE;
    }

    fout_params = (struct file_out_params *)malloc(sizeof(struct file_out_params));
    if (!fout_params) {
        goto fail;
    }

    fout_params->directory_path = directory_path;
    fout_params->name_suffix = name_suffix;

    _output_params = (struct output_params *)malloc(sizeof(struct output_params));
    if (!_output_params) {
        goto fail;
    }

    _output_params->method = OUTPUT_FILEOUT;
    _output_params->json_output_value = json_output_value;
    _output_params->params = fout_params;

    return SUCCESS;

fail:
    duderr("Out of memory");
    if (fout_params) {
        free(fout_params);
    }

    return FAILURE;
}

/**
 * Parse output method from JSON file and set parameters
 */
static int _set_output_params(void)
{
    struct json_value_t *json_output_value = NULL;
    const char *method = NULL;

    if (_json_root_value == NULL) {
        return FAILURE;
    }

    json_output_value = json_object_get_value(json_object(_json_root_value), "output");
    if (!json_output_value) {
        duderr("failed to parse JSON output value");
        return FAILURE;
    }

    method = json_object_get_string(json_object(json_output_value), "method");
    if (!method) {
        duderr("output method was not specified");
        return FAILURE;
    }

    if (!strcmp(method, "file-out")) {
       if (_set_file_out_params(json_output_value)) {
           return FAILURE;
       } 

       dudinfo("output parameters set to export files of suffix %s to directory path %s",
              _output_params->params->name_suffix, _output_params->params->directory_path);
    } else {
        duderr("unsupported export method: %s", method);
        return FAILURE;
    }

    return SUCCESS;
}

/**
 * Parse dudley JSON file and validate the schema
 */
int parse_dudley_file(const char *filepath)
{
    int ret = FAILURE;
    const char *name = NULL;
    struct json_value_t *schema = json_parse_string(
        "{"
            "\"name\":\"\","
            "\"input\": {},"
            "\"output\": {},"
            "\"events\": {}"
        "}"
    );

    _json_root_value = json_parse_file(filepath);
    if (!_json_root_value) {
        duderr("JSON formatted input is invalid");
        goto done;
    }

    if (json_validate(schema, _json_root_value) != JSONSuccess) {
        duderr("Erroneous JSON schema");
        json_value_free(_json_root_value);
        goto done;
    }

    name = json_object_get_string(json_object(_json_root_value), "name");
    dudinfo("%s loaded successfully!", filepath, name);
    printf("  -- NAME: %s\n", name);

    if (_set_input_params()) {
        duderr("Failed to parse and initialize the input parameters");
        goto done;
    }

    if (_set_output_params()) {
        duderr("Failed to parse and initialize the output parameters");
        goto done;
    }

    ret = SUCCESS;
done:
    json_value_free(schema);
    return ret;
}
