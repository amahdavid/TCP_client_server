#ifndef V1_ERROR_H
#define V1_ERROR_H


#include <stddef.h>


_Noreturn void fatal_errno(const char *file, const char *func, size_t line, int err_code, int exit_code);
_Noreturn void fatal_message(const char *file, const char *func, size_t line, const char *msg, int exit_code);


#endif //V1_ERROR_H
