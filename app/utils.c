#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Remove substring from string (buffer). */
void strremove(char *s, const char *toremove)
{
    while ((s = strstr(s, toremove)))
        memmove(s, s + strlen(toremove), 1 + strlen(s + strlen(toremove)));
}

/* Prints string with escape characters included. */
char *print_raw_string(char str[])
{
    char char_array[3];
    char *buffer = malloc(sizeof(char));
    unsigned int len = 1;
    unsigned int blk_size;

    while (*str != '\0')
    {
        blk_size = 2;
        switch (*str)
        {
        case '\n':
            strcpy(char_array, "\\n");
            break;
        case '\t':
            strcpy(char_array, "\\t");
            break;
        case '\v':
            strcpy(char_array, "\\v");
            break;
        case '\f':
            strcpy(char_array, "\\f");
            break;
        case '\a':
            strcpy(char_array, "\\a");
            break;
        case '\b':
            strcpy(char_array, "\\b");
            break;
        case '\r':
            strcpy(char_array, "\\r");
            break;
        default:
            sprintf(char_array, "%c", *str);
            blk_size = 1;
            break;
        }
        len += blk_size;
        buffer = realloc(buffer, len * sizeof(char));
        strcat(buffer, char_array);
        ++str;
    }
    return buffer;
}
