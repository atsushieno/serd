
#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>

void *abstract_stderr();
void* abstract_fopen(const char* path, const char* mode);
int abstract_fread(void *ptr, size_t size, size_t count, void* stream);
int abstract_vfprintf (void* stream, const char *format, va_list arg);
int abstract_fprintf (void* stream, const char *format, ...);
int abstract_ferror (void* stream);
int abstract_fclose (void* stream);
int abstract_fileno (void* stream);
int abstract_posix_fadvise(int fd, off_t offset, off_t len, int advice);
int abstract_getc (void* stream);

