#include <facetos/initrd.h>

#include <facetos/interfaces/IDirectory.h>
#include <facetos/interfaces/IFile.h>
#include <facetos/interfaces/IFileStore.h>
#include <facetos/interfaces/IGenericObject.h>
#include <facetos/interfaces/IUnixMetadata.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CPIO_HEADER_SIZE 110u
#define CPIO_MAX_ENTRIES 1024u
#define FACET_PATH_MAX 4096u

typedef struct InitrdEntry InitrdEntry;

struct InitrdEntry {
    FacetInitrd *owner;
    char *path;
    const uint8_t *data;
    size_t size;
    bool directory;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    IFile file;
    IDirectory directory_interface;
    IUnixMetadata unix_metadata;
    FacetHandle file_handle;
    FacetHandle directory_handle;
    FacetHandle unix_metadata_handle;
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
static FacetResult file_get_path(void *self, FacetString *out);
static FacetResult file_read_at(void *self, uint64_t offset, uint32_t maximum,
                                FacetArray_u8 *out);
static FacetResult file_write_at(void *self, uint64_t offset,
                                 const FacetArray_u8 *data, uint32_t *written);
static FacetResult directory_get_interface(void *self, uuid_t iid, FacetHandle *out);
static FacetResult directory_list(void *self, uint64_t cursor, uint32_t maximum,
                                  FacetArray_Entry *entries, uint64_t *next,
                                  bool *end);
static FacetResult directory_get_path(void *self, FacetString *path);
static FacetResult directory_open_file(void *self, const FacetString *path,
                                       FacetHandle *out);
static FacetResult directory_open_directory(void *self,
                                            const FacetString *path,
                                            FacetHandle *out);
static FacetResult metadata_get_interface(void *self, uuid_t iid,
                                          FacetHandle *out);
static FacetResult metadata_get_mode(void *self, uint32_t *out);
static FacetResult metadata_get_uid(void *self, uint32_t *out);
static FacetResult metadata_get_gid(void *self, uint32_t *out);

static FacetResult ensure_metadata_exported(InitrdEntry *entry)
{
    if (entry->unix_metadata_handle.platform != NULL) return FACET_OK;
    entry->unix_metadata = (IUnixMetadata){
        .self = entry,
        .priv = entry,
        .getInterface = metadata_get_interface,
        .getmode = metadata_get_mode,
        .getuid = metadata_get_uid,
        .getgid = metadata_get_gid,
    };
    return libfacet_export_interface(&entry->unix_metadata,
                                     &IUnixMetadata_MetaData,
                                     &entry->unix_metadata_handle);
}

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

static InitrdEntry *find_entry(FacetInitrd *initrd, const char *path,
                               bool directory)
{
    if (initrd == NULL || path == NULL || path[0] != '/')
        return NULL;
    size_t length = strlen(path);
    for (size_t i = 0; i < initrd->entry_count; i++) {
        InitrdEntry *entry = &initrd->entries[i];
        if (entry->directory == directory && strlen(entry->path) == length &&
            memcmp(entry->path, path, length) == 0)
            return entry;
    }
    return NULL;
}

static bool path_requires_directory(const FacetString *path)
{
    if (path->data[path->length - 1] == '/') return true;
    size_t start = path->length;
    while (start != 0 && path->data[start - 1] != '/') start--;
    size_t length = path->length - start;
    return (length == 1 && path->data[start] == '.') ||
           (length == 2 && path->data[start] == '.' &&
            path->data[start + 1] == '.');
}

/* Resolve a caller path against a canonical absolute directory. The result is
 * canonical, absolute, root-clamped, and owned by the caller. */
static FacetResult resolve_path(const char *base, const FacetString *path,
                                char **resolved, bool *requires_directory)
{
    if (resolved == NULL || requires_directory == NULL)
        return FACET_INVALID_ARGUMENT;
    *resolved = NULL;
    *requires_directory = false;
    if (base == NULL || base[0] != '/' || path == NULL || path->data == NULL ||
        path->length == 0 || path->length > FACET_PATH_MAX ||
        memchr(path->data, '\0', path->length) != NULL)
        return FACET_INVALID_ARGUMENT;

    char *output = malloc(FACET_PATH_MAX + 1u);
    if (output == NULL) return FACET_OUT_OF_MEMORY;
    size_t output_length;
    if (path->data[0] == '/') {
        output[0] = '/';
        output_length = 1;
    } else {
        output_length = strlen(base);
        if (output_length == 0 || output_length > FACET_PATH_MAX) {
            free(output);
            return FACET_INVALID_ARGUMENT;
        }
        memcpy(output, base, output_length);
    }

    for (size_t offset = 0; offset < path->length;) {
        while (offset < path->length && path->data[offset] == '/') offset++;
        size_t component = offset;
        while (offset < path->length && path->data[offset] != '/') offset++;
        size_t component_length = offset - component;
        if (component_length == 0 ||
            (component_length == 1 && path->data[component] == '.'))
            continue;
        if (component_length == 2 && path->data[component] == '.' &&
            path->data[component + 1] == '.') {
            if (output_length > 1) {
                while (output_length > 1 && output[output_length - 1] != '/')
                    output_length--;
                if (output_length > 1) output_length--;
            }
            continue;
        }
        size_t separator = output_length == 1 ? 0 : 1;
        if (output_length + separator > FACET_PATH_MAX ||
            component_length >
                FACET_PATH_MAX - (output_length + separator)) {
            free(output);
            return FACET_INVALID_ARGUMENT;
        }
        if (separator != 0) output[output_length++] = '/';
        memcpy(output + output_length, path->data + component,
               component_length);
        output_length += component_length;
    }
    output[output_length] = '\0';
    *requires_directory = path_requires_directory(path);
    *resolved = output;
    return FACET_OK;
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
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = (FacetHandle){0};
    char *resolved = NULL;
    bool requires_directory = false;
    FacetResult result = resolve_path("/", path, &resolved,
                                      &requires_directory);
    if (result != FACET_OK) return result;
    InitrdEntry *entry = requires_directory ? NULL :
        find_entry(self, resolved, false);
    free(resolved);
    if (entry == NULL) return FACET_NOT_FOUND;
    if (entry->file_handle.platform == NULL) {
        entry->file.self = entry; entry->file.priv = self;
        entry->file.getInterface = file_get_interface; entry->file.get_size = file_get_size;
        entry->file.read_at = file_read_at; entry->file.write_at = file_write_at;
        entry->file.getpath = file_get_path;
        if (libfacet_export_interface(&entry->file, &IFile_MetaData,
                                      &entry->file_handle) != FACET_OK)
            return FACET_OUT_OF_MEMORY;
    }
    return return_handle(entry->file_handle, out);
}

static FacetResult store_open_directory(void *self, const FacetString *path,
                                        FacetHandle *out)
{
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = (FacetHandle){0};
    char *resolved = NULL;
    bool requires_directory = false;
    FacetResult result = resolve_path("/", path, &resolved,
                                      &requires_directory);
    (void)requires_directory;
    if (result != FACET_OK) return result;
    InitrdEntry *entry = find_entry(self, resolved, true);
    free(resolved);
    if (entry == NULL) return FACET_NOT_FOUND;
    if (entry->directory_handle.platform == NULL) {
        entry->directory_interface.self = entry; entry->directory_interface.priv = self;
        entry->directory_interface.getInterface = directory_get_interface;
        entry->directory_interface.list = directory_list;
        entry->directory_interface.getpath = directory_get_path;
        entry->directory_interface.open_file = directory_open_file;
        entry->directory_interface.open_directory = directory_open_directory;
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
    if (iid_equal(iid, IID_IUnixMetadata)) {
        FacetResult result = ensure_metadata_exported(entry);
        return result == FACET_OK
            ? return_handle(entry->unix_metadata_handle, out) : result;
    }
    if (out != NULL) *out = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult file_get_path(void *self, FacetString *out)
{
    if (self == NULL || out == NULL) return FACET_INVALID_ARGUMENT;
    InitrdEntry *entry = self;
    out->data = entry->path;
    out->length = strlen(entry->path);
    return FACET_OK;
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
    if (iid_equal(iid, IID_IUnixMetadata)) {
        FacetResult result = ensure_metadata_exported(entry);
        return result == FACET_OK
            ? return_handle(entry->unix_metadata_handle, out) : result;
    }
    if (out != NULL) *out = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult metadata_get_interface(void *self, uuid_t iid,
                                          FacetHandle *out)
{
    InitrdEntry *entry = self;
    if (iid_equal(iid, IID_IGenericObject) || iid_equal(iid, IID_IUnixMetadata))
        return return_handle(entry->unix_metadata_handle, out);
    if (out != NULL) *out = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult metadata_get_mode(void *self, uint32_t *out)
{
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = ((InitrdEntry *)self)->mode;
    return FACET_OK;
}

static FacetResult metadata_get_uid(void *self, uint32_t *out)
{
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = ((InitrdEntry *)self)->uid;
    return FACET_OK;
}

static FacetResult metadata_get_gid(void *self, uint32_t *out)
{
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = ((InitrdEntry *)self)->gid;
    return FACET_OK;
}

static FacetResult directory_get_path(void *self, FacetString *path)
{
    if (self == NULL || path == NULL) return FACET_INVALID_ARGUMENT;
    InitrdEntry *entry = self;
    path->data = entry->path;
    path->length = strlen(entry->path);
    return FACET_OK;
}

static FacetResult directory_open_file(void *self, const FacetString *path,
                                       FacetHandle *out)
{
    if (self == NULL || out == NULL) return FACET_INVALID_ARGUMENT;
    *out = (FacetHandle){0};
    InitrdEntry *directory = self;
    FacetInitrd *initrd = directory->owner;
    char *resolved = NULL;
    bool requires_directory = false;
    FacetResult result = resolve_path(directory->path, path, &resolved,
                                      &requires_directory);
    if (result != FACET_OK) return result;
    FacetString absolute = {.data = resolved, .length = strlen(resolved)};
    result = requires_directory ? FACET_NOT_FOUND :
        store_open_file(initrd, &absolute, out);
    free(resolved);
    return result;
}

static FacetResult directory_open_directory(void *self,
                                            const FacetString *path,
                                            FacetHandle *out)
{
    if (self == NULL || out == NULL) return FACET_INVALID_ARGUMENT;
    *out = (FacetHandle){0};
    InitrdEntry *directory = self;
    FacetInitrd *initrd = directory->owner;
    if (initrd == NULL) return FACET_INVALID_HANDLE;
    char *resolved = NULL;
    bool requires_directory = false;
    FacetResult result = resolve_path(directory->path, path, &resolved,
                                      &requires_directory);
    (void)requires_directory;
    if (result != FACET_OK) return result;
    FacetString absolute = {.data = resolved, .length = strlen(resolved)};
    result = store_open_directory(initrd, &absolute, out);
    free(resolved);
    return result;
}

static bool immediate_child(const char *directory, const char *path, const char **name)
{
    size_t prefix;
    if (strcmp(directory, "/") == 0) {
        if (path[0] != '/' || path[1] == '\0') return false;
        prefix = 1;
    } else {
        size_t directory_length = strlen(directory);
        if (strncmp(path, directory, directory_length) != 0 ||
            path[directory_length] != '/' ||
            path[directory_length + 1] == '\0')
            return false;
        prefix = directory_length + 1;
    }
    *name = path + prefix;
    return strchr(*name, '/') == NULL;
}

static FacetResult directory_list(void *self, uint64_t cursor, uint32_t maximum,
                                  FacetArray_Entry *entries, uint64_t *next,
                                  bool *end)
{
    if (self == NULL || entries == NULL || next == NULL || end == NULL)
        return FACET_INVALID_ARGUMENT;
    InitrdEntry *directory = self;
    FacetInitrd *initrd = directory->owner;
    if (initrd == NULL) return FACET_INVALID_HANDLE;
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
    initrd->entries[0].owner = initrd;
    initrd->entries[0].directory = true;
    initrd->entries[0].mode = 0040755u;
    initrd->entry_count = 1;
    size_t offset = 0;
    while (offset + CPIO_HEADER_SIZE <= size) {
        const uint8_t *header = initrd->data + offset;
        if (memcmp(header, "070701", 6) != 0 && memcmp(header, "070702", 6) != 0) goto fail;
        size_t mode, uid, gid, file_size, name_size;
        if (hex_field(header + 14, 8, &mode) != 0 ||
            hex_field(header + 22, 8, &uid) != 0 ||
            hex_field(header + 30, 8, &gid) != 0 ||
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
        entry->owner = initrd;
        entry->path = path; entry->data = initrd->data + data_offset;
        entry->size = file_size; entry->directory = kind == 0040000u;
        entry->mode = (uint32_t)mode;
        entry->uid = (uint32_t)uid;
        entry->gid = (uint32_t)gid;
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
    FacetResult result = libfacet_export_interface(&initrd->store_interface,
                                                   &IFileStore_MetaData,
                                                   &initrd->store_handle);
    if (result != FACET_OK) return result;
    return return_handle(initrd->store_handle, store);
}

FacetResult facet_initrd_find_file(FacetInitrd *initrd, const char *path,
                                  const uint8_t **data, size_t *size)
{
    if (initrd == NULL || path == NULL || data == NULL || size == NULL)
        return FACET_INVALID_ARGUMENT;
    *data = NULL;
    *size = 0;
    FacetString name = {.data = path, .length = strlen(path)};
    if (name.length == 0 || name.length > FACET_PATH_MAX || name.data[0] != '/' ||
        memchr(name.data, '\0', name.length) != NULL)
        return FACET_NOT_FOUND;
    InitrdEntry *entry = find_entry(initrd, path, false);
    if (entry == NULL) return FACET_NOT_FOUND;
    *data = entry->data;
    *size = entry->size;
    return FACET_OK;
}

static bool entry_allows(const InitrdEntry *entry, uint32_t uid, uint32_t gid,
                         bool admin, unsigned requested)
{
    if (admin || uid == 0) {
        if ((requested & 1u) != 0 && (entry->mode & 0111u) == 0)
            return false;
        return true;
    }
    unsigned granted = uid == entry->uid ? (entry->mode >> 6) & 7u :
        gid == entry->gid ? (entry->mode >> 3) & 7u : entry->mode & 7u;
    return (granted & requested) == requested;
}

FacetResult facet_initrd_check_execute(FacetInitrd *initrd, const char *path,
                                      uint32_t uid, uint32_t gid, bool admin)
{
    if (initrd == NULL || path == NULL || path[0] != '/')
        return FACET_INVALID_ARGUMENT;
    size_t length = strlen(path);
    if (length == 0 || length > FACET_PATH_MAX)
        return FACET_INVALID_ARGUMENT;
    InitrdEntry *root = find_entry(initrd, "/", true);
    if (root == NULL || !entry_allows(root, uid, gid, admin, 1u))
        return FACET_ACCESS_DENIED;
    char *prefix = malloc(length + 1);
    if (prefix == NULL) return FACET_OUT_OF_MEMORY;
    FacetResult result = FACET_OK;
    for (size_t i = 1; i < length;) {
        while (i < length && path[i] == '/') i++;
        size_t end = i;
        while (end < length && path[end] != '/') end++;
        if (end == length) break;
        memcpy(prefix, path, end);
        prefix[end] = '\0';
        InitrdEntry *directory = find_entry(initrd, prefix, true);
        if (directory == NULL) {
            result = FACET_NOT_FOUND;
            break;
        }
        if (!entry_allows(directory, uid, gid, admin, 1u)) {
            result = FACET_ACCESS_DENIED;
            break;
        }
        i = end;
    }
    if (result == FACET_OK) {
        InitrdEntry *file = find_entry(initrd, path, false);
        if (file == NULL) result = FACET_NOT_FOUND;
        else if (!entry_allows(file, uid, gid, admin, 1u))
            result = FACET_ACCESS_DENIED;
    }
    free(prefix);
    return result;
}

FacetResult facet_initrd_open_node(FacetInitrd *initrd, const char *base,
                                  const FacetString *path, bool directory,
                                  FacetInitrdNode **node)
{
    if (initrd == NULL || base == NULL || node == NULL)
        return FACET_INVALID_ARGUMENT;
    *node = NULL;
    char *resolved = NULL;
    bool requires_directory = false;
    FacetResult result = resolve_path(base, path, &resolved,
                                      &requires_directory);
    if (result != FACET_OK) return result;
    InitrdEntry *found = requires_directory && !directory ? NULL :
        find_entry(initrd, resolved, directory);
    free(resolved);
    if (found == NULL) return FACET_NOT_FOUND;
    *node = found;
    return FACET_OK;
}

const char *facet_initrd_node_path(const FacetInitrdNode *node)
{
    return node == NULL ? NULL : node->path;
}

FacetResult facet_initrd_node_metadata(const FacetInitrdNode *node,
                                      uint32_t *mode, uint32_t *uid,
                                      uint32_t *gid)
{
    if (node == NULL || mode == NULL || uid == NULL || gid == NULL)
        return FACET_INVALID_ARGUMENT;
    *mode = node->mode;
    *uid = node->uid;
    *gid = node->gid;
    return FACET_OK;
}

FacetResult facet_initrd_node_metadata_handle(FacetInitrdNode *node,
                                             FacetHandle *handle)
{
    if (node == NULL || handle == NULL) return FACET_INVALID_ARGUMENT;
    *handle = (FacetHandle){0};
    FacetResult result = ensure_metadata_exported(node);
    return result == FACET_OK ? return_handle(node->unix_metadata_handle,
                                               handle) : result;
}

FacetResult facet_initrd_node_size(const FacetInitrdNode *node,
                                  uint64_t *size)
{
    if (node == NULL || size == NULL || node->directory)
        return FACET_INVALID_ARGUMENT;
    *size = node->size;
    return FACET_OK;
}

FacetResult facet_initrd_node_read(const FacetInitrdNode *node,
                                  uint64_t offset, uint32_t maximum,
                                  FacetArray_u8 *data)
{
    if (node == NULL || node->directory)
        return FACET_INVALID_ARGUMENT;
    return file_read_at((void *)(uintptr_t)node, offset, maximum, data);
}

FacetResult facet_initrd_node_list(const FacetInitrdNode *node,
                                  uint64_t cursor, uint32_t maximum,
                                  FacetArray_Entry *entries,
                                  uint64_t *next_cursor, bool *end)
{
    if (node == NULL || !node->directory)
        return FACET_INVALID_ARGUMENT;
    return directory_list((void *)(uintptr_t)node, cursor, maximum, entries,
                          next_cursor, end);
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
        if (initrd->entries[i].unix_metadata_handle.platform != NULL)
            (void)libfacet_unexport_interface(initrd->entries[i].unix_metadata_handle);
        free(initrd->entries[i].path);
    }
    free(initrd->entries); free(initrd);
}
