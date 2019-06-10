
#if ANDROID
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#else
#include <fcntl.h>
#endif

#include "abstract_io.h"

#if ANDROID

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

#else

void *abstract_stderr()
{
    return stderr;
}

void* abstract_fopen(const char* path, const char* mode)
{
    return fopen(path, mode);
}

int abstract_fread(void *ptr, size_t size, size_t count, void* stream)
{
    return fread(ptr, size, count, stream);
}

int abstract_vfprintf (void* stream, const char *format, va_list arg)
{
    return vfprintf (stream, format, arg);
}

int abstract_fprintf (void* stream, const char *format, ...)
{
    va_list ap;
    va_start (ap, format);
    return fprintf (stream, format, ap);
}

int abstract_ferror (void* stream)
{
    return ferror (stream);
}

int abstract_fclose (void* stream)
{
    return fclose (stream);
}

int abstract_fileno (void* stream)
{
    return fileno (stream);
}

int abstract_posix_fadvise(int fd, off_t offset, off_t len, int advice)
{
    return posix_fadvise (fd, offset, len, advice);
}

int abstract_getc (void* stream)
{
    return getc (stream);
}

#endif
