#include <facetos/initrd.h>

#include <facetos/interfaces/IDirectory.h>
#include <facetos/interfaces/IFile.h>
#include <facetos/interfaces/IFileStore.h>
#include <facetos/interfaces/IGenericObject.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CPIO_HEADER_SIZE 110u
#define CPIO_MAX_ENTRIES 1024u

typedef struct InitrdEntry InitrdEntry;

struct InitrdEntry {
    char *path;
    const uint8_t *data;
    size_t size;
    bool directory;
    IFile file;
    IDirectory directory_interface;
    FacetHandle file_handle;
    FacetHandle directory_handle;
};

struct FacetInitrd {
    const uint8_t *data;
    size_t size;
    InitrdEntry *entries;
    size_t entry_count;
    IFileStore store_interface;
    FacetHandle store_handle;
};

static bool iid_equal(uuid_t a, uuid_t b)
{
    return memcmp(a.bytes, b.bytes, sizeof(a.bytes)) == 0;
}

static FacetResult return_handle(FacetHandle handle, FacetHandle *out)
{
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = (FacetHandle){0};
    if (handle.platform == NULL) return FACET_INVALID_HANDLE;
    *out = handle;
    return FACET_OK;
}

static FacetResult file_get_interface(void *self, uuid_t iid, FacetHandle *out);
static FacetResult file_get_size(void *self, uint64_t *out);
static FacetResult file_read_at(void *self, uint64_t offset, uint32_t maximum,
                                FacetArray_u8 *out);
static FacetResult file_write_at(void *self, uint64_t offset,
                                 const FacetArray_u8 *data, uint32_t *written);
static FacetResult directory_get_interface(void *self, uuid_t iid, FacetHandle *out);
static FacetResult directory_list(void *self, uint64_t cursor, uint32_t maximum,
                                  FacetArray_Entry *entries, uint64_t *next,
                                  bool *end);

static size_t align4(size_t value)
{
    return (value + 3u) & ~((size_t)3u);
}

static int hex_field(const uint8_t *text, size_t count, size_t *out)
{
    size_t value = 0;
    for (size_t i = 0; i < count; i++) {
        unsigned digit;
        if (text[i] >= '0' && text[i] <= '9') digit = text[i] - '0';
        else if (text[i] >= 'a' && text[i] <= 'f') digit = text[i] - 'a' + 10;
        else if (text[i] >= 'A' && text[i] <= 'F') digit = text[i] - 'A' + 10;
        else return -1;
        if (value > (SIZE_MAX - digit) / 16u) return -1;
        value = value * 16u + digit;
    }
    *out = value;
    return 0;
}

static char *canonical_path(const uint8_t *name, size_t length)
{
    while (length >= 2 && name[0] == '.' && name[1] == '/') {
        name += 2; length -= 2;
    }
    while (length != 0 && name[0] == '/') {
        name++; length--;
    }
    if (length == 0 || (length == 1 && name[0] == '.'))
        return NULL;
    for (size_t i = 0; i < length; i++)
        if (name[i] == '\0' || (name[i] == '/' && i + 1 < length && name[i + 1] == '/') ||
            (name[i] == '.' && i + 1 < length && name[i + 1] == '.' &&
             (i == 0 || name[i - 1] == '/') &&
             (i + 2 == length || name[i + 2] == '/')))
            return NULL;
    char *result = malloc(length + 2);
    if (result == NULL) return NULL;
    result[0] = '/';
    memcpy(result + 1, name, length);
    result[length + 1] = '\0';
    return result;
}

static InitrdEntry *find_entry(FacetInitrd *initrd, const FacetString *path,
                               bool directory)
{
    if (initrd == NULL || path == NULL || path->data == NULL ||
        path->length == 0 || path->length > 4096 || path->data[0] != '/')
        return NULL;
    for (size_t i = 0; i < initrd->entry_count; i++) {
        InitrdEntry *entry = &initrd->entries[i];
        if (entry->directory == directory && strlen(entry->path) == path->length &&
            memcmp(entry->path, path->data, path->length) == 0)
            return entry;
    }
    return NULL;
}

static FacetResult store_get_interface(void *self, uuid_t iid, FacetHandle *out)
{
    FacetInitrd *initrd = self;
    if (iid_equal(iid, IID_IGenericObject) || iid_equal(iid, IID_IFileStore))
        return return_handle(initrd->store_handle, out);
    if (out != NULL) *out = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult store_open_file(void *self, const FacetString *path, FacetHandle *out)
{
    InitrdEntry *entry = find_entry(self, path, false);
    if (entry == NULL) return FACET_NOT_FOUND;
    if (entry->file_handle.platform == NULL) {
        entry->file.self = entry; entry->file.priv = self;
        entry->file.getInterface = file_get_interface; entry->file.get_size = file_get_size;
        entry->file.read_at = file_read_at; entry->file.write_at = file_write_at;
        if (libfacet_export_interface(&entry->file, &IFile_MetaData,
                                      &entry->file_handle) != FACET_OK)
            return FACET_OUT_OF_MEMORY;
    }
    return return_handle(entry->file_handle, out);
}

static FacetResult store_open_directory(void *self, const FacetString *path,
                                        FacetHandle *out)
{
    InitrdEntry *entry = find_entry(self, path, true);
    if (entry == NULL) return FACET_NOT_FOUND;
    if (entry->directory_handle.platform == NULL) {
        entry->directory_interface.self = entry; entry->directory_interface.priv = self;
        entry->directory_interface.getInterface = directory_get_interface;
        entry->directory_interface.list = directory_list;
        if (libfacet_export_interface(&entry->directory_interface, &IDirectory_MetaData,
                                      &entry->directory_handle) != FACET_OK)
            return FACET_OUT_OF_MEMORY;
    }
    return return_handle(entry->directory_handle, out);
}

static FacetResult file_get_interface(void *self, uuid_t iid, FacetHandle *out)
{
    InitrdEntry *entry = self;
    if (iid_equal(iid, IID_IGenericObject) || iid_equal(iid, IID_IFile))
        return return_handle(entry->file_handle, out);
    if (out != NULL) *out = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult file_get_size(void *self, uint64_t *out)
{
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = ((InitrdEntry *)self)->size;
    return FACET_OK;
}

static FacetResult file_read_at(void *self, uint64_t offset, uint32_t maximum,
                                FacetArray_u8 *out)
{
    InitrdEntry *entry = self;
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    out->data = NULL; out->count = 0;
    if (offset > entry->size) return FACET_INVALID_ARGUMENT;
    size_t remaining = entry->size - (size_t)offset;
    size_t count = remaining < maximum ? remaining : maximum;
    out->data = (uint8_t *)(uintptr_t)(entry->data + offset);
    out->count = count;
    return FACET_OK;
}

static FacetResult file_write_at(void *self, uint64_t offset,
                                 const FacetArray_u8 *data, uint32_t *written)
{
    (void)self; (void)offset; (void)data;
    if (written == NULL) return FACET_INVALID_ARGUMENT;
    *written = 0;
    return FACET_NOT_SUPPORTED;
}

static FacetResult directory_get_interface(void *self, uuid_t iid, FacetHandle *out)
{
    InitrdEntry *entry = self;
    if (iid_equal(iid, IID_IGenericObject) || iid_equal(iid, IID_IDirectory))
        return return_handle(entry->directory_handle, out);
    if (out != NULL) *out = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static bool immediate_child(const char *directory, const char *path, const char **name)
{
    size_t prefix = strcmp(directory, "/") == 0 ? 1 : strlen(directory) + 1;
    if (strcmp(directory, "/") != 0 && strncmp(path, directory, prefix - 1) != 0)
        return false;
    if (strncmp(path, directory, prefix) != 0 || path[prefix] == '\0') return false;
    *name = path + prefix;
    return strchr(*name, '/') == NULL;
}

static FacetResult directory_list(void *self, uint64_t cursor, uint32_t maximum,
                                  FacetArray_Entry *entries, uint64_t *next,
                                  bool *end)
{
    InitrdEntry *directory = self;
    FacetInitrd *initrd = directory->directory_interface.priv;
    if (entries == NULL || next == NULL || end == NULL) return FACET_INVALID_ARGUMENT;
    entries->data = NULL; entries->count = 0; *next = cursor; *end = true;
    if (cursor > initrd->entry_count) return FACET_INVALID_ARGUMENT;
    size_t count = 0;
    Entry *result = maximum == 0 ? NULL : calloc(maximum, sizeof(*result));
    if (maximum != 0 && result == NULL) return FACET_OUT_OF_MEMORY;
    size_t seen = 0;
    for (size_t i = 0; i < initrd->entry_count; i++) {
        const char *name;
        if (!immediate_child(directory->path, initrd->entries[i].path, &name)) continue;
        if (seen++ < cursor) continue;
        if (count == maximum) { *end = false; break; }
        result[count].name.data = name;
        result[count].name.length = strlen(name);
        result[count].type = initrd->entries[i].directory ? EntryType_Directory : EntryType_File;
        count++;
    }
    entries->data = result; entries->count = count; *next = cursor + count;
    return FACET_OK;
}

FacetInitrd *facet_initrd_create(const void *data, size_t size)
{
    if (data == NULL || size < CPIO_HEADER_SIZE) return NULL;
    FacetInitrd *initrd = calloc(1, sizeof(*initrd));
    if (initrd == NULL) return NULL;
    initrd->data = data; initrd->size = size;
    initrd->entries = calloc(1, sizeof(*initrd->entries));
    if (initrd->entries == NULL) goto fail;
    initrd->entries[0].path = malloc(2);
    if (initrd->entries[0].path == NULL) goto fail;
    strcpy(initrd->entries[0].path, "/");
    initrd->entries[0].directory = true;
    initrd->entry_count = 1;
    size_t offset = 0;
    while (offset + CPIO_HEADER_SIZE <= size) {
        const uint8_t *header = initrd->data + offset;
        if (memcmp(header, "070701", 6) != 0 && memcmp(header, "070702", 6) != 0) goto fail;
        size_t mode, file_size, name_size;
        if (hex_field(header + 14, 8, &mode) != 0 ||
            hex_field(header + 54, 8, &file_size) != 0 ||
            hex_field(header + 94, 8, &name_size) != 0 || name_size == 0) goto fail;
        size_t name_offset = offset + CPIO_HEADER_SIZE;
        if (name_offset > size || name_size > size - name_offset) goto fail;
        const uint8_t *name = initrd->data + name_offset;
        if (name[name_size - 1] != '\0') goto fail;
        size_t data_offset = align4(name_offset + name_size);
        if (data_offset > size || file_size > size - data_offset) goto fail;
        if (strcmp((const char *)name, "TRAILER!!!") == 0) break;
        if (strcmp((const char *)name, ".") == 0) {
            offset = align4(data_offset + file_size);
            continue;
        }
        unsigned kind = (unsigned)(mode & 0170000u);
        if (kind != 0100000u && kind != 0040000u) goto fail;
        if (initrd->entry_count == CPIO_MAX_ENTRIES) goto fail;
        char *path = canonical_path(name, name_size - 1);
        if (path == NULL) goto fail;
        for (size_t i = 0; i < initrd->entry_count; i++)
            if (strcmp(path, initrd->entries[i].path) == 0) { free(path); goto fail; }
        InitrdEntry *grown = realloc(initrd->entries,
                                     (initrd->entry_count + 1) * sizeof(*grown));
        if (grown == NULL) { free(path); goto fail; }
        initrd->entries = grown;
        InitrdEntry *entry = &initrd->entries[initrd->entry_count++];
        memset(entry, 0, sizeof(*entry));
        entry->path = path; entry->data = initrd->data + data_offset;
        entry->size = file_size; entry->directory = kind == 0040000u;
        offset = align4(data_offset + file_size);
    }
    return initrd;
fail:
    facet_initrd_destroy(initrd);
    return NULL;
}

FacetResult facet_initrd_export(FacetInitrd *initrd, FacetHandle *store)
{
    if (initrd == NULL || store == NULL) return FACET_INVALID_ARGUMENT;
    if (initrd->store_handle.platform != NULL) return return_handle(initrd->store_handle, store);
    initrd->store_interface.self = initrd; initrd->store_interface.priv = initrd;
    initrd->store_interface.getInterface = store_get_interface;
    initrd->store_interface.open_file = store_open_file;
    initrd->store_interface.open_directory = store_open_directory;
    if (libfacet_export_interface(&initrd->store_interface, &IFileStore_MetaData,
                                  &initrd->store_handle) != FACET_OK) return FACET_ERROR;
    return return_handle(initrd->store_handle, store);
}

void facet_initrd_destroy(FacetInitrd *initrd)
{
    if (initrd == NULL) return;
    if (initrd->store_handle.platform != NULL) (void)libfacet_unexport_interface(initrd->store_handle);
    for (size_t i = 0; i < initrd->entry_count; i++) {
        if (initrd->entries[i].file_handle.platform != NULL)
            (void)libfacet_unexport_interface(initrd->entries[i].file_handle);
        if (initrd->entries[i].directory_handle.platform != NULL)
            (void)libfacet_unexport_interface(initrd->entries[i].directory_handle);
        free(initrd->entries[i].path);
    }
    free(initrd->entries); free(initrd);
}
