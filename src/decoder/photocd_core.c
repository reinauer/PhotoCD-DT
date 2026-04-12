#include "photocd_priv.h"

void pcd_set_error_string(photocdDecode *self, const char *message)
{
	if (message == NULL) {
		message = "Unknown error";
	}

	strncpy(self->errorString, message, sizeof(self->errorString) - 1);
	self->errorString[sizeof(self->errorString) - 1] = '\0';
}

bool pcd_fail(const char **error, const char *message)
{
	if (error != NULL && *error == NULL) {
		*error = message;
	}

	return false;
}

void pcd_set_error_with_suffix(photocdDecode *self, const char *message,
	const char *fallback, const char *suffix)
{
	const char *base = message != NULL ? message : fallback;

	pcd_set_error_string(self, base);
	if (suffix != NULL && suffix[0] != '\0' &&
		strlen(self->errorString) + strlen(suffix) < sizeof(self->errorString)) {
		strcat(self->errorString, suffix);
	}
}

#if defined(__amigaos__) || defined(__AMIGA__) || defined(AMIGA)
struct pcd_file {
	BPTR handle;
	LONG position;
	BOOL eof;
};

static LONG pcd_file_update_position(pcd_file *file)
{
	LONG pos;

	pos = Seek(file->handle, 0, OFFSET_CURRENT);
	if (pos != -1) {
		file->position = pos;
	}
	return pos;
}

pcd_file *pcd_file_open(const pcdFilenameType *path)
{
	pcd_file *file;
	BPTR handle;

	handle = Open(path, MODE_OLDFILE);
	if (handle == 0) {
		return NULL;
	}

	file = (pcd_file *)malloc(sizeof(*file));
	if (file == NULL) {
		Close(handle);
		return NULL;
	}

	file->handle = handle;
	file->position = 0;
	file->eof = FALSE;
	return file;
}

void pcd_file_close(pcd_file *file)
{
	if (file != NULL) {
		if (file->handle != 0) {
			Close(file->handle);
		}
		free(file);
	}
}

size_t pcd_file_read(pcd_file *file, void *buffer, size_t size)
{
	LONG actual;

	if ((file == NULL) || (size == 0)) {
		return 0;
	}

	actual = Read(file->handle, buffer, (LONG)size);
	if (actual < 0) {
		file->eof = TRUE;
		return 0;
	}

	file->position += actual;
	if ((size_t)actual < size) {
		file->eof = TRUE;
	}
	return (size_t)actual;
}

int pcd_file_getc(pcd_file *file)
{
	uint8_t value;

	if (pcd_file_read(file, &value, 1) != 1) {
		return EOF;
	}
	return value;
}

bool pcd_file_seek(pcd_file *file, long offset, int origin)
{
	LONG mode;

	switch (origin) {
		case PCD_SEEK_CUR:
			mode = OFFSET_CURRENT;
			break;
		case PCD_SEEK_END:
			mode = OFFSET_END;
			break;
		case PCD_SEEK_SET:
		default:
			mode = OFFSET_BEGINNING;
			break;
	}

	if (Seek(file->handle, offset, mode) == -1) {
		return false;
	}

	file->eof = FALSE;
	return pcd_file_update_position(file) != -1;
}

long pcd_file_tell(pcd_file *file)
{
	return pcd_file_update_position(file);
}

size_t pcd_file_read_items(pcd_file *file, void *buffer, size_t item_size,
	size_t item_count)
{
	size_t bytes;
	size_t actual;

	if ((item_size == 0) || (item_count == 0)) {
		return 0;
	}

	bytes = item_size * item_count;
	actual = pcd_file_read(file, buffer, bytes);
	return actual / item_size;
}
#else
pcd_file *pcd_file_open(const pcdFilenameType *path)
{
	return pcdMagicFOpen(path, pcdMagicFOpenMode);
}

void pcd_file_close(pcd_file *file)
{
	if (file != NULL) {
		fclose(file);
	}
}

size_t pcd_file_read(pcd_file *file, void *buffer, size_t size)
{
	return fread(buffer, 1, size, file);
}

int pcd_file_getc(pcd_file *file)
{
	return getc(file);
}

bool pcd_file_seek(pcd_file *file, long offset, int origin)
{
	int whence;

	switch (origin) {
		case PCD_SEEK_CUR:
			whence = SEEK_CUR;
			break;
		case PCD_SEEK_END:
			whence = SEEK_END;
			break;
		case PCD_SEEK_SET:
		default:
			whence = SEEK_SET;
			break;
	}

	return fseek(file, offset, whence) == 0;
}

long pcd_file_tell(pcd_file *file)
{
	return ftell(file);
}

size_t pcd_file_read_items(pcd_file *file, void *buffer, size_t item_size,
	size_t item_count)
{
	return fread(buffer, item_size, item_count, file);
}
#endif

static void pcd_format_unsigned(char *dest, unsigned int value)
{
	char reverse[16];
	size_t len = 0;

	do {
		reverse[len++] = (char)('0' + (value % 10));
		value /= 10;
	} while ((value != 0) && (len < sizeof(reverse)));

	while (len > 0) {
		*dest++ = reverse[--len];
	}
	*dest = '\0';
}

void pcd_format_version(char *dest, unsigned int major, unsigned int minor)
{
	size_t len;

	pcd_format_unsigned(dest, major);
	len = strlen(dest);
	dest[len++] = '.';
	pcd_format_unsigned(dest + len, minor);
}

void pcd_format_scanner_pixel_size(char *dest, const uint8_t *value)
{
	dest[0] = (char)('0' + ((value[0] >> 4) & 0x0f));
	dest[1] = (char)('0' + (value[0] & 0x0f));
	dest[2] = '.';
	dest[3] = (char)('0' + ((value[1] >> 4) & 0x0f));
	dest[4] = (char)('0' + (value[1] & 0x0f));
	dest[5] = '\0';
}
