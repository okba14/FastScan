#ifndef FASTSCAN_PARSER_H
#define FASTSCAN_PARSER_H

#include "safe_types.h"

// Parser Context
typedef struct {
    const char* pattern;
    fs_size_t pattern_len;
    fs_byte_t first_byte; 
} fs_parser_t;


void fs_parser_init(fs_parser_t* parser, const char* pattern, fs_size_t len);


int fs_parser_is_match(fs_parser_t* parser, const fs_byte_t* data, fs_size_t data_len, fs_size_t pos);

#endif // FASTSCAN_PARSER_H
