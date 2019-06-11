
#if !__ABSTRACT_H_INCLUDED__
#define __ABSTRACT_H_INCLUDED__

#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>

/* nothing for desktop, AAssetManager* for Android */
void abstract_set_io_context (void* ioContext);

/* serd specific */
void* abstract_fopen(const char* path, const char* mode);
int abstract_fread(void *ptr, size_t size, size_t count, void* stream);
int abstract_error_vfprintf (const char *format, va_list arg);
int abstract_error_fprintf (const char *format, ...);
int abstract_ferror (void* stream);
int abstract_fclose (void* stream);
int abstract_fileno (void* stream);
int abstract_getc (void* stream);

#endif
