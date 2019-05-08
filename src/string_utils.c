//
// Created by qianbiao on 5/11/19.
//

#include <stddef.h>
#include <string.h>
#include <string_utils.h>

/**
 * check whether source string starts with prefix string
 *
 * @param source
 * @param prefix
 * @return
 */
bool utool_str_starts_with(const char *source, const char *prefix)
{
    size_t len_source = strlen(source),
            len_prefix = strlen(prefix);
    return len_source < len_prefix ? false : strncmp(prefix, source, len_prefix) == 0;
}


/**
 * check whether source string starts with prefix string while ignoring differences in case
 *
 * @param source
 * @param prefix
 * @return
 */
bool utool_str_case_starts_with(const char *source, const char *prefix)
{
    size_t len_source = strlen(source),
            len_prefix = strlen(prefix);
    return len_source < len_prefix ? false : strncasecmp(prefix, source, len_prefix) == 0;
}

