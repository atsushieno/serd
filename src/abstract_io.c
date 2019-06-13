
#if ANDROID
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#else
#include <fcntl.h>
#endif

#include "abstract_io.h"


#if ANDROID

AAssetManager *current_asset_manager;

void abstract_set_io_context (void* ioContext)
{
	current_asset_manager = (AAssetManager*) ioContext;
}


void* abstract_fopen(const char* path, const char* mode)
{
	char *pathFixed = NULL;
	if (path [0] == '/') {
		pathFixed = calloc(strlen(path) + 1, 1);
		pathFixed [0] = '.';
		strcpy (pathFixed + 1, path);
	}
	void *ret = AAssetManager_open(current_asset_manager, pathFixed != NULL ? pathFixed : path, AASSET_MODE_UNKNOWN);
	if (pathFixed)
		free (pathFixed);
	return ret;
}

int abstract_fread(void *ptr, size_t size, size_t count, void* stream)
{
	return AAsset_read((AAsset*) stream, ptr, size * count);
}

int abstract_error_vfprintf (const char *format, va_list arg)
{
    return vfprintf (stderr, format, arg);
}

int abstract_ferror (void* stream)
{
	/* not much we can do here */
	return ferror((FILE*) stream);
}

int abstract_fclose (void* stream)
{
	AAsset_close((AAsset*) stream);
	return 0;
}

int abstract_getc (void* stream)
{
	char buf[1];
	if (AAsset_read(stream, &buf, 1) <= 0)
		return -1;
	return buf [0];
}

#else

void abstract_set_io_context (void* ioContext)
{
}


void* abstract_fopen(const char* path, const char* mode)
{
    return fopen(path, mode);
}

int abstract_fread(void *ptr, size_t size, size_t count, void* stream)
{
    return fread(ptr, size, count, stream);
}

int abstract_error_vfprintf (const char *format, va_list arg)
{
    return vfprintf (stderr, format, arg);
}

int abstract_ferror (void* stream)
{
    return ferror (stream);
}

int abstract_fclose (void* stream)
{
    return fclose (stream);
}

int abstract_getc (void* stream)
{
    return getc (stream);
}

#endif

int abstract_error_fprintf (const char *format, ...)
{
    va_list ap;
    va_start (ap, format);
    return abstract_error_vfprintf (format, ap);
}
