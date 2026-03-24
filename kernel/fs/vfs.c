#include <stddef.h>
#include "fs/vfs.h"
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// Seek to a new offset in the file
int vfs_lseek(file_t *f, size_t offset, int whence) {
	if (!f) return -1;
	size_t new_offset = 0;
	switch (whence) {
		case SEEK_SET:
			new_offset = offset;
			break;
		case SEEK_CUR:
			new_offset = f->offset + offset;
			break;
		case SEEK_END:
			if (f->entry && f->entry->size)
				new_offset = f->entry->size + offset;
			else
				return -1;
			break;
		default:
			return -1;
	}
	// Optionally, check bounds (0 <= new_offset <= file size)
	if (f->entry && new_offset > f->entry->size)
		return -1;
	f->offset = new_offset;
	return new_offset;
}
