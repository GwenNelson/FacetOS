#include <facetos/dominit0/file_view.h>

#include <facetos/interfaces/IDirectory.h>
#include <facetos/interfaces/IFile.h>
#include <facetos/interfaces/IFileStore.h>
#include <facetos/interfaces/IGenericObject.h>
#include <facetos/interfaces/IUnixMetadata.h>

#include <stdlib.h>
#include <string.h>

typedef struct CredentialNode CredentialNode;

struct Dominit0CredentialFileStore {
    IFileStore interface;
    FacetHandle handle;
    FacetInitrd *initrd;
    uint32_t uid;
    uint32_t gid;
    bool admin;
    size_t references;
    CredentialNode *nodes;
    Dominit0CredentialFileStore *next;
};

static Dominit0CredentialFileStore *credential_stores;

struct CredentialNode {
    Dominit0CredentialFileStore *store;
    FacetInitrdNode *backing;
    bool directory;
    FacetHandle handle;
    union {
        IFile file;
        IDirectory directory_interface;
    } interface;
    CredentialNode *next;
};

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
    if (iid_equal(iid, IID_IUnixMetadata))
        return facet_initrd_node_metadata_handle(node->backing, out);
    return FACET_NO_INTERFACE;
}

static FacetResult node_path(void *self, FacetString *out)
{
    CredentialNode *node = self;
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    const char *path = facet_initrd_node_path(node->backing);
    if (path == NULL) return FACET_INVALID_HANDLE;
    out->data = path;
    out->length = strlen(path);
    return FACET_OK;
}

static FacetResult file_size(void *self, uint64_t *out)
{
    return facet_initrd_node_size(((CredentialNode *)self)->backing, out);
}

static FacetResult file_read(void *self, uint64_t offset, uint32_t maximum,
                             FacetArray_u8 *out)
{
    CredentialNode *node = self;
    FacetResult result = check_access(node->store, node->backing, 4u);
    return result == FACET_OK ? facet_initrd_node_read(
        node->backing, offset, maximum, out) : result;
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
                             FacetInitrdNode *backing, bool directory,
                             FacetHandle *out);

static FacetResult directory_list(void *self, uint64_t cursor,
                                  uint32_t maximum, FacetArray_Entry *entries,
                                  uint64_t *next, bool *end)
{
    CredentialNode *node = self;
    FacetResult result = check_access(node->store, node->backing, 5u);
    return result == FACET_OK ? facet_initrd_node_list(
        node->backing, cursor, maximum, entries, next, end) : result;
}

static FacetResult directory_open(void *self, const FacetString *path,
                                  bool directory, FacetHandle *out)
{
    CredentialNode *node = self;
    FacetInitrdNode *opened = NULL;
    FacetResult result = facet_initrd_open_node(
        node->store->initrd, facet_initrd_node_path(node->backing), path,
        directory, &opened);
    if (result != FACET_OK) return result;
    result = check_search(node->store, facet_initrd_node_path(opened),
                          directory);
    return result == FACET_OK ? wrap_node(node->store, opened, directory, out) :
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
                             FacetInitrdNode *backing, bool directory,
                             FacetHandle *out)
{
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = (FacetHandle){0};
    const char *path = facet_initrd_node_path(backing);
    for (CredentialNode *candidate = store->nodes; candidate != NULL;
         candidate = candidate->next) {
        const char *candidate_path = facet_initrd_node_path(candidate->backing);
        if (candidate->directory == directory && path != NULL &&
            candidate_path != NULL && strcmp(path, candidate_path) == 0)
            return clone_handle(candidate->handle, out);
    }
    CredentialNode *node = calloc(1, sizeof(*node));
    if (node == NULL) return FACET_OUT_OF_MEMORY;
    node->store = store;
    node->backing = backing;
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
    FacetInitrdNode *opened = NULL;
    FacetResult result = facet_initrd_open_node(store->initrd, "/", path,
                                                directory, &opened);
    if (result != FACET_OK) return result;
    result = check_search(store, facet_initrd_node_path(opened), directory);
    return result == FACET_OK ? wrap_node(store, opened, directory, out) :
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
    free(store);
}
