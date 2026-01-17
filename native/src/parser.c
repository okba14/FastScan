#include "parser.h"
#include <string.h>

void fs_parser_init(fs_parser_t* parser, const char* pattern, fs_size_t len) {
    parser->pattern = pattern;
    parser->pattern_len = len;
    parser->first_byte = (len > 0) ? (fs_byte_t)pattern[0] : 0;
}


int fs_parser_is_match(fs_parser_t* parser, const fs_byte_t* data, fs_size_t data_len, fs_size_t pos) {

    if (pos + parser->pattern_len > data_len) {
        return 0;
    }


    if (data[pos] != parser->first_byte) {
        return 0;
    }


    return memcmp(&data[pos], parser->pattern, parser->pattern_len) == 0;
}
