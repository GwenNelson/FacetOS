#include <facetos/dominit0/file_view.h>
#include <facetos/dominit0/config.h>

#include <facetos/interfaces/IDirectory.h>
#include <facetos/interfaces/IFile.h>
#include <facetos/interfaces/IFileStore.h>
#include <facetos/interfaces/IGenericObject.h>
#include <facetos/interfaces/IUnixMetadata.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

extern Dominit0SystemConfig *dominit0_config_get_system(void)
    __attribute__((weak));

typedef struct CredentialNode CredentialNode;

typedef enum SyntheticNode {
    SYNTHETIC_NONE,
    SYNTHETIC_ETC_DIRECTORY,
    SYNTHETIC_PASSWD,
    SYNTHETIC_SHADOW,
    SYNTHETIC_FSTAB,
} SyntheticNode;

/* This is a mount supplied by the domain namespace, never an initrd entry.
 * IPOSIXView presents the same objects as /etc after it roots itself at
 * /posix.  Keep the contents here with the native file-store implementation
 * so both views have one authority and one access check. */
static const char default_passwd[] =
    "root:x:0:0:root:/home/root:/bin/sh\n"
    "user:x:1000:1000:user:/home/user:/bin/sh\n";
static const char default_shadow[] =
    "root:$5$facet$j7FgoXidvJl10CTaW0nguGP3ZnvKnqS3/IHmDVliPQ9:::::::\n"
    "user:$5$facet$j7FgoXidvJl10CTaW0nguGP3ZnvKnqS3/IHmDVliPQ9:::::::\n";
static const char default_fstab[] = "initrd / initrd ro 0 0\n";

struct Dominit0CredentialFileStore {
    IFileStore interface;
    FacetHandle handle;
    FacetInitrd *initrd;
    uint32_t uid;
    uint32_t gid;
    bool admin;
    char *virtual_passwd;
    char *virtual_shadow;
    size_t references;
    CredentialNode *nodes;
    Dominit0CredentialFileStore *next;
};

static Dominit0CredentialFileStore *credential_stores;

struct CredentialNode {
    Dominit0CredentialFileStore *store;
    FacetInitrdNode *backing;
    SyntheticNode synthetic;
    bool directory;
    FacetHandle handle;
    union {
        IFile file;
        IDirectory directory_interface;
    } interface;
    CredentialNode *next;
};

static const char *synthetic_path(SyntheticNode node)
{
    switch (node) {
    case SYNTHETIC_ETC_DIRECTORY: return "/posix/etc";
    case SYNTHETIC_PASSWD: return "/posix/etc/passwd";
    case SYNTHETIC_SHADOW: return "/posix/etc/shadow";
    case SYNTHETIC_FSTAB: return "/posix/etc/fstab";
    default: return NULL;
    }
}

static const char *synthetic_contents(const Dominit0CredentialFileStore *store,
                                      SyntheticNode node)
{
    switch (node) {
    case SYNTHETIC_PASSWD: return store->virtual_passwd;
    case SYNTHETIC_SHADOW: return store->virtual_shadow;
    case SYNTHETIC_FSTAB: return default_fstab;
    default: return NULL;
    }
}

static SyntheticNode synthetic_for_path(const char *path, bool directory)
{
    if (path == NULL) return SYNTHETIC_NONE;
    if (directory && strcmp(path, "/posix/etc") == 0)
        return SYNTHETIC_ETC_DIRECTORY;
    if (!directory && strcmp(path, "/posix/etc/passwd") == 0)
        return SYNTHETIC_PASSWD;
    if (!directory && strcmp(path, "/posix/etc/shadow") == 0)
        return SYNTHETIC_SHADOW;
    if (!directory && strcmp(path, "/posix/etc/fstab") == 0)
        return SYNTHETIC_FSTAB;
    return SYNTHETIC_NONE;
}

static bool iid_equal(uuid_t left, uuid_t right)
{
    return memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

static FacetResult clone_handle(FacetHandle handle, FacetHandle *out)
{
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = (FacetHandle){0};
    return handle.platform == NULL ? FACET_INVALID_HANDLE :
        libfacet_handle_clone(handle, out);
}

static bool mode_allows(uint32_t mode, uint32_t owner, uint32_t group,
                        uint32_t uid, uint32_t gid, bool admin,
                        unsigned requested)
{
    if (admin || uid == 0) {
        if ((requested & 1u) != 0 && (mode & 0111u) == 0) return false;
        return true;
    }
    unsigned granted = uid == owner ? (mode >> 6) & 7u :
        gid == group ? (mode >> 3) & 7u : mode & 7u;
    return (granted & requested) == requested;
}

static FacetResult check_access(Dominit0CredentialFileStore *store,
                                FacetInitrdNode *node, unsigned requested)
{
    uint32_t mode = 0, owner = 0, group = 0;
    FacetResult result = facet_initrd_node_metadata(node, &mode, &owner,
                                                    &group);
    if (result != FACET_OK) return result;
    return mode_allows(mode, owner, group, store->uid, store->gid,
                       store->admin, requested) ? FACET_OK :
        FACET_ACCESS_DENIED;
}

static FacetResult check_search(Dominit0CredentialFileStore *store,
                                const char *path, bool include_target)
{
    if (path == NULL || path[0] != '/') return FACET_INVALID_ARGUMENT;
    size_t length = strlen(path);
    FacetString root_path = {.data = "/", .length = 1};
    FacetInitrdNode *directory = NULL;
    FacetResult result = facet_initrd_open_node(store->initrd, "/", &root_path,
                                                true, &directory);
    if (result == FACET_OK) result = check_access(store, directory, 1u);
    char *prefix = malloc(length + 1);
    if (prefix == NULL) return FACET_OUT_OF_MEMORY;
    for (size_t i = 1; result == FACET_OK && i < length;) {
        while (i < length && path[i] == '/') i++;
        size_t end = i;
        while (end < length && path[end] != '/') end++;
        if (end == length && !include_target) break;
        memcpy(prefix, path, end);
        prefix[end] = '\0';
        FacetString candidate = {.data = prefix, .length = end};
        directory = NULL;
        result = facet_initrd_open_node(store->initrd, "/", &candidate, true,
                                        &directory);
        if (result == FACET_OK) result = check_access(store, directory, 1u);
        i = end;
    }
    free(prefix);
    return result;
}

static FacetResult node_get_interface(void *self, uuid_t iid, FacetHandle *out)
{
    CredentialNode *node = self;
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = (FacetHandle){0};
    if (iid_equal(iid, IID_IGenericObject) ||
        (!node->directory && iid_equal(iid, IID_IFile)) ||
        (node->directory && iid_equal(iid, IID_IDirectory)))
        return clone_handle(node->handle, out);
    if (node->synthetic == SYNTHETIC_NONE && iid_equal(iid, IID_IUnixMetadata))
        return facet_initrd_node_metadata_handle(node->backing, out);
    return FACET_NO_INTERFACE;
}

static FacetResult node_path(void *self, FacetString *out)
{
    CredentialNode *node = self;
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    const char *path = node->synthetic == SYNTHETIC_NONE ?
        facet_initrd_node_path(node->backing) : synthetic_path(node->synthetic);
    if (path == NULL) return FACET_INVALID_HANDLE;
    out->data = path;
    out->length = strlen(path);
    return FACET_OK;
}

static FacetResult file_size(void *self, uint64_t *out)
{
    CredentialNode *node = self;
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    const char *contents = synthetic_contents(node->store, node->synthetic);
    if (contents != NULL) { *out = strlen(contents); return FACET_OK; }
    return facet_initrd_node_size(node->backing, out);
}

static FacetResult file_read(void *self, uint64_t offset, uint32_t maximum,
                             FacetArray_u8 *out)
{
    CredentialNode *node = self;
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = (FacetArray_u8){0};
    const char *contents = synthetic_contents(node->store, node->synthetic);
    if (contents != NULL) {
        if (node->synthetic == SYNTHETIC_SHADOW && !node->store->admin)
            return FACET_ACCESS_DENIED;
        size_t length = strlen(contents);
        if (offset >= length) return FACET_OK;
        size_t count = length - (size_t)offset;
        if (count > maximum) count = maximum;
        out->data = malloc(count);
        if (out->data == NULL && count != 0) return FACET_OUT_OF_MEMORY;
        memcpy(out->data, contents + offset, count);
        out->count = count;
        return FACET_OK;
    }
    FacetResult result = check_access(node->store, node->backing, 4u);
    return result == FACET_OK ? facet_initrd_node_read(node->backing, offset,
                                                       maximum, out) : result;
}

static FacetResult file_write(void *self, uint64_t offset,
                              const FacetArray_u8 *data, uint32_t *written)
{
    (void)self;
    (void)offset;
    (void)data;
    if (written == NULL) return FACET_INVALID_ARGUMENT;
    *written = 0;
    return FACET_NOT_SUPPORTED;
}

static FacetResult wrap_node(Dominit0CredentialFileStore *store,
                             FacetInitrdNode *backing, SyntheticNode synthetic,
                             bool directory,
                             FacetHandle *out);

static FacetResult directory_list(void *self, uint64_t cursor,
                                  uint32_t maximum, FacetArray_Entry *entries,
                                  uint64_t *next, bool *end)
{
    CredentialNode *node = self;
    if (node->synthetic == SYNTHETIC_ETC_DIRECTORY) {
        static const char *names[] = {"fstab", "passwd", "shadow"};
        if (entries == NULL || next == NULL || end == NULL) return FACET_INVALID_ARGUMENT;
        *entries = (FacetArray_Entry){0};
        if (cursor >= sizeof(names) / sizeof(names[0]) || maximum == 0) {
            *next = cursor; *end = cursor >= sizeof(names) / sizeof(names[0]);
            return FACET_OK;
        }
        size_t count = sizeof(names) / sizeof(names[0]) - (size_t)cursor;
        if (count > maximum) count = maximum;
        entries->data = calloc(count, sizeof(*entries->data));
        if (entries->data == NULL) return FACET_OUT_OF_MEMORY;
        entries->count = count;
        for (size_t i = 0; i < count; i++) {
            entries->data[i].name.data = strdup(names[cursor + i]);
            if (entries->data[i].name.data == NULL) return FACET_OUT_OF_MEMORY;
            entries->data[i].name.length = strlen(names[cursor + i]);
            entries->data[i].type = EntryType_File;
        }
        *next = cursor + count;
        *end = *next == sizeof(names) / sizeof(names[0]);
        return FACET_OK;
    }
    FacetResult result = check_access(node->store, node->backing, 5u);
    if (result != FACET_OK) return result;
    result = facet_initrd_node_list(node->backing, cursor, maximum, entries,
                                    next, end);
    if (result != FACET_OK || cursor != 0 || !*end || maximum <= entries->count ||
        strcmp(facet_initrd_node_path(node->backing), "/posix") != 0)
        return result;
    Entry *extended = realloc(entries->data, (entries->count + 1) * sizeof(*extended));
    if (extended == NULL) return FACET_OUT_OF_MEMORY;
    entries->data = extended;
    entries->data[entries->count].name.data = strdup("etc");
    if (entries->data[entries->count].name.data == NULL) return FACET_OUT_OF_MEMORY;
    entries->data[entries->count].name.length = 3;
    entries->data[entries->count].type = EntryType_Directory;
    entries->count++;
    return FACET_OK;
}

static FacetResult directory_open(void *self, const FacetString *path,
                                  bool directory, FacetHandle *out)
{
    CredentialNode *node = self;
    if (path == NULL || path->data == NULL || path->length == 0) return FACET_INVALID_ARGUMENT;
    const char *base = node->synthetic == SYNTHETIC_NONE ?
        facet_initrd_node_path(node->backing) : synthetic_path(node->synthetic);
    size_t base_length = strlen(base);
    size_t length = path->length;
    bool absolute = path->data[0] == '/';
    char *candidate = malloc((absolute ? 0 : base_length + 1) + length + 1);
    if (candidate == NULL) return FACET_OUT_OF_MEMORY;
    if (absolute) memcpy(candidate, path->data, length);
    else { memcpy(candidate, base, base_length); candidate[base_length] = '/';
           memcpy(candidate + base_length + 1, path->data, length); }
    candidate[(absolute ? 0 : base_length + 1) + length] = '\0';
    /* Credential views must preserve ordinary directory semantics too. */
    char *scan = candidate, *write = candidate;
    while (*scan) {
        while (*scan == '/') scan++;
        if (!*scan) break;
        char *part = scan; while (*scan && *scan != '/') scan++;
        size_t n = (size_t)(scan - part);
        if (n == 1 && part[0] == '.') continue;
        if (n == 2 && part[0] == '.' && part[1] == '.') {
            while (write > candidate + 1 && write[-1] != '/') write--;
            if (write > candidate + 1) write--;
            continue;
        }
        if (write == candidate || write[-1] != '/') *write++ = '/';
        for (size_t i = 0; i < n; i++) write[i] = part[i];
        write += n;
    }
    if (write == candidate) *write++ = '/';
    *write = '\0';
    SyntheticNode synthetic = synthetic_for_path(candidate, directory);
    if (synthetic != SYNTHETIC_NONE) {
        free(candidate);
        return wrap_node(node->store, NULL, synthetic, directory, out);
    }
    FacetInitrdNode *opened = NULL;
    FacetString normalized = {.data = candidate, .length = strlen(candidate)};
    FacetResult result = facet_initrd_open_node(node->store->initrd, "/",
                                                &normalized, directory, &opened);
    free(candidate);
    if (result != FACET_OK) return result;
    result = check_search(node->store, facet_initrd_node_path(opened),
                          directory);
    return result == FACET_OK ? wrap_node(node->store, opened, SYNTHETIC_NONE, directory, out) :
        result;
}

static FacetResult directory_open_file(void *self, const FacetString *path,
                                       FacetHandle *out)
{
    return directory_open(self, path, false, out);
}

static FacetResult directory_open_directory(void *self,
                                            const FacetString *path,
                                            FacetHandle *out)
{
    return directory_open(self, path, true, out);
}

static FacetResult wrap_node(Dominit0CredentialFileStore *store,
                             FacetInitrdNode *backing, SyntheticNode synthetic,
                             bool directory,
                             FacetHandle *out)
{
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = (FacetHandle){0};
    const char *path = synthetic == SYNTHETIC_NONE ?
        facet_initrd_node_path(backing) : synthetic_path(synthetic);
    for (CredentialNode *candidate = store->nodes; candidate != NULL;
         candidate = candidate->next) {
        const char *candidate_path = candidate->synthetic == SYNTHETIC_NONE ?
            facet_initrd_node_path(candidate->backing) :
            synthetic_path(candidate->synthetic);
        if (candidate->directory == directory && path != NULL &&
            candidate_path != NULL && strcmp(path, candidate_path) == 0)
            return clone_handle(candidate->handle, out);
    }
    CredentialNode *node = calloc(1, sizeof(*node));
    if (node == NULL) return FACET_OUT_OF_MEMORY;
    node->store = store;
    node->backing = backing;
    node->synthetic = synthetic;
    node->directory = directory;
    FacetResult result;
    if (directory) {
        node->interface.directory_interface = (IDirectory){
            .self = node, .priv = node, .getInterface = node_get_interface,
            .list = directory_list, .getpath = node_path,
            .open_file = directory_open_file,
            .open_directory = directory_open_directory,
        };
        result = libfacet_export_interface(&node->interface.directory_interface,
                                           &IDirectory_MetaData,
                                           &node->handle);
    } else {
        node->interface.file = (IFile){
            .self = node, .priv = node, .getInterface = node_get_interface,
            .get_size = file_size, .read_at = file_read,
            .write_at = file_write, .getpath = node_path,
        };
        result = libfacet_export_interface(&node->interface.file,
                                           &IFile_MetaData, &node->handle);
    }
    if (result != FACET_OK) {
        free(node);
        return result;
    }
    node->next = store->nodes;
    store->nodes = node;
    return clone_handle(node->handle, out);
}

static FacetResult store_get_interface(void *self, uuid_t iid,
                                       FacetHandle *out)
{
    Dominit0CredentialFileStore *store = self;
    if (!iid_equal(iid, IID_IGenericObject) &&
        !iid_equal(iid, IID_IFileStore)) {
        if (out != NULL) *out = (FacetHandle){0};
        return FACET_NO_INTERFACE;
    }
    return clone_handle(store->handle, out);
}

static FacetResult store_open(void *self, const FacetString *path,
                              bool directory, FacetHandle *out)
{
    Dominit0CredentialFileStore *store = self;
    if (path == NULL || path->data == NULL || path->length == 0)
        return FACET_INVALID_ARGUMENT;
    char *requested = strndup(path->data, path->length);
    if (requested == NULL) return FACET_OUT_OF_MEMORY;
    SyntheticNode synthetic = synthetic_for_path(requested, directory);
    free(requested);
    if (synthetic != SYNTHETIC_NONE)
        return wrap_node(store, NULL, synthetic, directory, out);
    FacetInitrdNode *opened = NULL;
    FacetResult result = facet_initrd_open_node(store->initrd, "/", path,
                                                directory, &opened);
    if (result != FACET_OK) return result;
    result = check_search(store, facet_initrd_node_path(opened), directory);
    return result == FACET_OK ? wrap_node(store, opened, SYNTHETIC_NONE, directory, out) :
        result;
}

static FacetResult store_open_file(void *self, const FacetString *path,
                                   FacetHandle *out)
{
    return store_open(self, path, false, out);
}

static FacetResult store_open_directory(void *self, const FacetString *path,
                                        FacetHandle *out)
{
    return store_open(self, path, true, out);
}

static int render_virtual_credentials(Dominit0CredentialFileStore *store)
{
    Dominit0SystemConfig *system = dominit0_config_get_system == NULL ? NULL :
        dominit0_config_get_system();
    if (system == NULL || system->parsed.user_count == 0) {
        store->virtual_passwd = strdup(default_passwd);
        store->virtual_shadow = strdup(default_shadow);
        return store->virtual_passwd == NULL || store->virtual_shadow == NULL ? -1 : 0;
    }
    size_t passwd_size = 1, shadow_size = 1;
    for (size_t i = 0; i < system->parsed.user_count; i++) {
        const FacetConfigUser *user = &system->parsed.users[i];
        passwd_size += strlen(user->name) + strlen(user->home_path) +
            strlen(user->posix_shell) + 48;
        shadow_size += strlen(user->name) + strlen(user->password_hash) + 16;
    }
    store->virtual_passwd = calloc(1, passwd_size);
    store->virtual_shadow = calloc(1, shadow_size);
    if (store->virtual_passwd == NULL || store->virtual_shadow == NULL) return -1;
    size_t passwd_used = 0, shadow_used = 0;
    for (size_t i = 0; i < system->parsed.user_count; i++) {
        const FacetConfigUser *user = &system->parsed.users[i];
        int written = snprintf(store->virtual_passwd + passwd_used,
            passwd_size - passwd_used, "%s:x:%u:%u:%s:%s:%s\n", user->name,
            user->uid, user->gid, user->name, user->home_path, user->posix_shell);
        if (written < 0 || (size_t)written >= passwd_size - passwd_used) return -1;
        passwd_used += (size_t)written;
        written = snprintf(store->virtual_shadow + shadow_used,
            shadow_size - shadow_used, "%s:%s:::::::\n", user->name,
            user->password_hash);
        if (written < 0 || (size_t)written >= shadow_size - shadow_used) return -1;
        shadow_used += (size_t)written;
    }
    return 0;
}

Dominit0CredentialFileStore *dominit0_credential_file_store_create(
    FacetInitrd *initrd, uint32_t uid, uint32_t gid, bool admin)
{
    if (initrd == NULL) return NULL;
    for (Dominit0CredentialFileStore *candidate = credential_stores;
         candidate != NULL; candidate = candidate->next) {
        if (candidate->initrd == initrd && candidate->uid == uid &&
            candidate->gid == gid && candidate->admin == admin) {
            candidate->references++;
            return candidate;
        }
    }
    Dominit0CredentialFileStore *store = calloc(1, sizeof(*store));
    if (store == NULL) return NULL;
    store->initrd = initrd;
    store->uid = uid;
    store->gid = gid;
    store->admin = admin;
    store->references = 1;
    if (render_virtual_credentials(store) != 0) {
        free(store->virtual_passwd);
        free(store->virtual_shadow);
        free(store);
        return NULL;
    }
    store->interface = (IFileStore){
        .self = store, .priv = store, .getInterface = store_get_interface,
        .open_file = store_open_file,
        .open_directory = store_open_directory,
    };
    if (libfacet_export_interface(&store->interface, &IFileStore_MetaData,
                                  &store->handle) != FACET_OK) {
        free(store);
        return NULL;
    }
    store->next = credential_stores;
    credential_stores = store;
    return store;
}

FacetHandle dominit0_credential_file_store_handle(
    const Dominit0CredentialFileStore *store)
{
    return store == NULL ? (FacetHandle){0} : store->handle;
}

const char *dominit0_credential_file_store_passwd(
    const Dominit0CredentialFileStore *store)
{
    return store == NULL ? NULL : store->virtual_passwd;
}

const char *dominit0_credential_file_store_shadow(
    const Dominit0CredentialFileStore *store)
{
    return store == NULL ? NULL : store->virtual_shadow;
}

const char *dominit0_credential_file_store_fstab(
    const Dominit0CredentialFileStore *store)
{
    return store == NULL ? NULL : default_fstab;
}

void dominit0_credential_file_store_destroy(
    Dominit0CredentialFileStore *store)
{
    if (store == NULL) return;
    if (store->references > 1) {
        store->references--;
        return;
    }
    Dominit0CredentialFileStore **link = &credential_stores;
    while (*link != NULL && *link != store) link = &(*link)->next;
    if (*link == store) *link = store->next;
    while (store->nodes != NULL) {
        CredentialNode *node = store->nodes;
        store->nodes = node->next;
        if (node->handle.platform != NULL)
            (void)libfacet_unexport_interface(node->handle);
        free(node);
    }
    if (store->handle.platform != NULL)
        (void)libfacet_unexport_interface(store->handle);
    free(store->virtual_passwd);
    free(store->virtual_shadow);
    free(store);
}
