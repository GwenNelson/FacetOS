#include <facetos/dominit0/auth.h>

#include <facetos/dominit0/environment.h>
#include <facetos/interfaces/IAuthService.h>
#include <facetos/interfaces/IAuthenticatedPrincipal.h>
#include <facetos/interfaces/IHumanUser.h>
#include <facetos/interfaces/IPrincipal.h>
#include <facetos/interfaces/ISecurityManager.h>
#include <facetos/interfaces/ISession.h>
#include <facetos/sha256.h>

#include <stdlib.h>
#include <string.h>

typedef struct AuthAuthority AuthAuthority;
typedef struct AuthUser AuthUser;
typedef struct Session Session;

struct AuthUser {
    const FacetConfigUser *config;
    AuthAuthority *authority;
    IPrincipal principal;
    IHumanUser human;
    IAuthenticatedPrincipal authenticated;
    FacetHandle principal_handle;
    FacetHandle human_handle;
    FacetHandle authenticated_handle;
};

struct Session {
    ISession interface;
    FacetHandle handle;
    FacetHandle principal;
    uint64_t domain_id;
    const FacetConfigUser *user;
    Session *next;
};

struct AuthAuthority {
    IAuthService service;
    ISecurityManager security;
    FacetHandle service_handle;
    FacetHandle security_handle;
    AuthUser *users;
    size_t user_count;
    Session *sessions;
    uint64_t domain_id;
};

static AuthAuthority *authorities;
static size_t authority_count;

static FacetResult create_session_for_user(AuthAuthority *authority,
                                           AuthUser *user,
                                           FacetHandle *out);

static bool iid_equal(uuid_t left, uuid_t right)
{
    return memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

static FacetResult clone_handle(FacetHandle handle, FacetHandle *out)
{
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = (FacetHandle){0};
    if (handle.platform == NULL) return FACET_INVALID_HANDLE;
    return libfacet_handle_clone(handle, out);
}

static FacetResult principal_get_interface(void *self, uuid_t iid,
                                           FacetHandle *out)
{
    AuthUser *user = self;
    if (iid_equal(iid, IID_IGenericObject) || iid_equal(iid, IID_IPrincipal))
        return clone_handle(user->principal_handle, out);
    if (iid_equal(iid, IID_IHumanUser))
        return clone_handle(user->human_handle, out);
    if (out != NULL) *out = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult principal_id(void *self, uuid_t *out)
{
    AuthUser *user = self;
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    uint8_t digest[32];
    facet_sha256((const uint8_t *)user->config->name,
                 strlen(user->config->name), digest);
    memcpy(out->bytes, digest, sizeof(out->bytes));
    return FACET_OK;
}

static FacetResult principal_name(void *self, FacetString *out)
{
    AuthUser *user = self;
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    out->data = user->config->name;
    out->length = strlen(user->config->name);
    return FACET_OK;
}

static FacetResult human_get_interface(void *self, uuid_t iid,
                                       FacetHandle *out)
{
    AuthUser *user = self;
    if (iid_equal(iid, IID_IGenericObject) || iid_equal(iid, IID_IHumanUser))
        return clone_handle(user->human_handle, out);
    if (iid_equal(iid, IID_IPrincipal))
        return clone_handle(user->principal_handle, out);
    if (out != NULL) *out = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult human_full_name(void *self, FacetString *out)
{
    return principal_name(self, out);
}

static FacetResult human_home(void *self, FacetString *out)
{
    AuthUser *user = self;
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    out->data = user->config->home_path;
    out->length = strlen(out->data);
    return FACET_OK;
}

static FacetResult human_uid(void *self, uint32_t *out)
{
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = ((AuthUser *)self)->config->uid;
    return FACET_OK;
}

static FacetResult human_gid(void *self, uint32_t *out)
{
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = ((AuthUser *)self)->config->gid;
    return FACET_OK;
}

static FacetResult human_admin(void *self, bool *out)
{
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = ((AuthUser *)self)->config->admin;
    return FACET_OK;
}

static FacetResult human_shell(void *self, FacetString *out)
{
    AuthUser *user = self;
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    out->data = user->config->native_shell != NULL
        ? user->config->native_shell : "/FacetOS/FacetShell";
    out->length = strlen(out->data);
    return FACET_OK;
}

static FacetResult human_pronouns(void *self, FacetHandle *out)
{
    (void)self;
    if (out != NULL) *out = (FacetHandle){0};
    return FACET_NOT_FOUND;
}

static FacetResult authenticated_get_interface(void *self, uuid_t iid,
                                               FacetHandle *out)
{
    AuthUser *user = self;
    if (iid_equal(iid, IID_IGenericObject) ||
        iid_equal(iid, IID_IAuthenticatedPrincipal))
        return clone_handle(user->authenticated_handle, out);
    if (out != NULL) *out = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult authenticated_principal(void *self, FacetHandle *out)
{
    return clone_handle(((AuthUser *)self)->principal_handle, out);
}

static FacetResult authenticated_domain_id(void *self, uint64_t *out)
{
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = ((AuthUser *)self)->authority->domain_id;
    return FACET_OK;
}

static int export_user(AuthUser *user)
{
    if (user->authenticated_handle.platform != NULL) return 0;
    user->principal.self = user;
    user->principal.priv = user;
    user->principal.getInterface = principal_get_interface;
    user->principal.getid = principal_id;
    user->principal.getname = principal_name;
    user->human.self = user;
    user->human.priv = user;
    user->human.getInterface = human_get_interface;
    user->human.getfull_name = human_full_name;
    user->human.gethome_path = human_home;
    user->human.getdefault_shell = human_shell;
    user->human.getpronouns = human_pronouns;
    user->human.getuid = human_uid;
    user->human.getgid = human_gid;
    user->human.getadmin = human_admin;
    user->authenticated.self = user;
    user->authenticated.priv = user;
    user->authenticated.getInterface = authenticated_get_interface;
    user->authenticated.get_principal = authenticated_principal;
    user->authenticated.get_domain_id = authenticated_domain_id;
    if (libfacet_export_interface(&user->principal, &IPrincipal_MetaData,
                                  &user->principal_handle) != FACET_OK ||
        libfacet_export_interface(&user->human, &IHumanUser_MetaData,
                                  &user->human_handle) != FACET_OK ||
        libfacet_export_interface(&user->authenticated,
                                  &IAuthenticatedPrincipal_MetaData,
                                  &user->authenticated_handle) != FACET_OK)
        return -1;
    return 0;
}

static FacetResult service_get_interface(void *self, uuid_t iid,
                                         FacetHandle *out)
{
    AuthAuthority *authority = self;
    if (iid_equal(iid, IID_IGenericObject) || iid_equal(iid, IID_IAuthService))
        return clone_handle(authority->service_handle, out);
    if (out != NULL) *out = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static unsigned hex_nibble(char value)
{
    return value >= '0' && value <= '9'
        ? (unsigned)(value - '0') : (unsigned)(value - 'a' + 10);
}

static bool constant_time_equal(const void *left, const void *right, size_t size)
{
    const uint8_t *a = left;
    const uint8_t *b = right;
    uint8_t difference = 0;
    for (size_t i = 0; i < size; i++) difference |= a[i] ^ b[i];
    return difference == 0;
}

static FacetResult authenticate(void *self, const FacetString *name,
                                const FacetString *password, FacetHandle *out)
{
    AuthAuthority *authority = self;
    if (out != NULL) *out = (FacetHandle){0};
    if (name == NULL || password == NULL || out == NULL ||
        name->data == NULL || password->data == NULL || password->length > 4096)
        return FACET_INVALID_ARGUMENT;
    uint8_t digest[32];
    uint8_t expected[32];
    facet_sha256((const uint8_t *)password->data, password->length, digest);
    for (size_t i = 0; i < authority->user_count; i++) {
        AuthUser *user = &authority->users[i];
        if (strlen(user->config->name) != name->length ||
            !constant_time_equal(user->config->name, name->data, name->length))
            continue;
        for (size_t j = 0; j < sizeof(expected); j++)
            expected[j] = (uint8_t)((hex_nibble(user->config->password_sha256[j * 2]) << 4) |
                                    hex_nibble(user->config->password_sha256[j * 2 + 1]));
        if (!constant_time_equal(digest, expected, sizeof(digest))) break;
        if (export_user(user) != 0) return FACET_OUT_OF_MEMORY;
        return clone_handle(user->authenticated_handle, out);
    }
    return FACET_ACCESS_DENIED;
}

static FacetResult session_get_interface(void *self, uuid_t iid,
                                         FacetHandle *out)
{
    Session *session = self;
    if (iid_equal(iid, IID_IGenericObject) || iid_equal(iid, IID_ISession))
        return clone_handle(session->handle, out);
    if (out != NULL) *out = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult session_principal(void *self, FacetHandle *out)
{
    return clone_handle(((Session *)self)->principal, out);
}

static FacetResult session_domain_id(void *self, uint64_t *out)
{
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = ((Session *)self)->domain_id;
    return FACET_OK;
}

static FacetResult session_credentials(void *self, uint32_t *uid,
                                       uint32_t *gid, bool *admin)
{
    Session *session = self;
    if (uid == NULL || gid == NULL || admin == NULL || session->user == NULL)
        return FACET_INVALID_ARGUMENT;
    *uid = session->user->uid;
    *gid = session->user->gid;
    *admin = session->user->admin;
    return FACET_OK;
}

static FacetResult security_get_interface(void *self, uuid_t iid,
                                          FacetHandle *out)
{
    AuthAuthority *authority = self;
    if (iid_equal(iid, IID_IGenericObject) ||
        iid_equal(iid, IID_ISecurityManager))
        return clone_handle(authority->security_handle, out);
    if (out != NULL) *out = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static AuthUser *find_authenticated_user(AuthAuthority *authority,
                                         FacetHandle authenticated,
                                         FacetHandle *principal)
{
    FacetHandle owned = {0};
    if (libfacet_handle_clone(authenticated, &owned) != FACET_OK) return NULL;
    IAuthenticatedPrincipal *proof = libfacet_proxy_from_handle(
        &IAuthenticatedPrincipal_MetaData, owned);
    if (proof == NULL) return NULL;
    FacetResult result = proof->get_principal(proof->self, principal);
    uint64_t proof_domain_id = UINT64_MAX;
    if (result == FACET_OK)
        result = proof->get_domain_id(proof->self, &proof_domain_id);
    libfacet_free_proxy_client(proof);
    if (result != FACET_OK || principal->platform == NULL ||
        proof_domain_id != authority->domain_id)
        return NULL;

    FacetHandle principal_copy = {0};
    if (libfacet_handle_clone(*principal, &principal_copy) != FACET_OK)
        return NULL;
    IPrincipal *object = libfacet_proxy_from_handle(&IPrincipal_MetaData,
                                                    principal_copy);
    FacetString name = {0};
    result = object == NULL ? FACET_ACCESS_DENIED :
        object->getname(object->self, &name);
    libfacet_free_proxy_client(object);
    if (result != FACET_OK) return NULL;
    AuthUser *match = NULL;
    for (size_t i = 0; i < authority->user_count; i++) {
        const char *candidate = authority->users[i].config->name;
        if (strlen(candidate) == name.length &&
            constant_time_equal(candidate, name.data, name.length)) {
            match = &authority->users[i];
            break;
        }
    }
    free((void *)name.data);
    return match;
}

static FacetResult security_create(void *self, FacetHandle authenticated,
                                   FacetHandle *out)
{
    AuthAuthority *authority = self;
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = (FacetHandle){0};
    FacetHandle principal = {0};
    AuthUser *authenticated_user =
        find_authenticated_user(authority, authenticated, &principal);
    if (authenticated_user == NULL) {
        if (principal.platform != NULL) (void)libfacet_handle_release(principal);
        return FACET_ACCESS_DENIED;
    }
    (void)libfacet_handle_release(principal);
    return create_session_for_user(authority, authenticated_user, out);
}

static FacetResult create_session_for_user(AuthAuthority *authority,
                                           AuthUser *user,
                                           FacetHandle *out)
{
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = (FacetHandle){0};
    if (authority == NULL || user == NULL || export_user(user) != 0)
        return FACET_OUT_OF_MEMORY;
    Session *session = calloc(1, sizeof(*session));
    if (session == NULL) return FACET_OUT_OF_MEMORY;
    if (libfacet_handle_clone(user->principal_handle, &session->principal) !=
        FACET_OK) {
        free(session);
        return FACET_OUT_OF_MEMORY;
    }
    session->domain_id = authority->domain_id;
    session->user = user->config;
    session->interface.self = session;
    session->interface.priv = session;
    session->interface.getInterface = session_get_interface;
    session->interface.get_principal = session_principal;
    session->interface.get_domain_id = session_domain_id;
    session->interface.get_credentials = session_credentials;
    if (libfacet_export_interface(&session->interface, &ISession_MetaData,
                                  &session->handle) != FACET_OK) {
        (void)libfacet_handle_release(session->principal);
        free(session);
        return FACET_OUT_OF_MEMORY;
    }
    session->next = authority->sessions;
    authority->sessions = session;
    return clone_handle(session->handle, out);
}

FacetResult dominit0_auth_session_for_user(uint64_t domain_id,
                                           const char *name,
                                           FacetHandle *out)
{
    if (name == NULL || out == NULL) return FACET_INVALID_ARGUMENT;
    *out = (FacetHandle){0};
    for (size_t i = 0; i < authority_count; i++) {
        AuthAuthority *authority = &authorities[i];
        if (authority->domain_id != domain_id) continue;
        for (size_t j = 0; j < authority->user_count; j++) {
            AuthUser *user = &authority->users[j];
            if (strcmp(user->config->name, name) == 0)
                return create_session_for_user(authority, user, out);
        }
        return FACET_NOT_FOUND;
    }
    return FACET_NOT_FOUND;
}

static void destroy_authority(AuthAuthority *authority)
{
    while (authority->sessions != NULL) {
        Session *session = authority->sessions;
        authority->sessions = session->next;
        if (session->handle.platform != NULL)
            (void)libfacet_unexport_interface(session->handle);
        if (session->principal.platform != NULL)
            (void)libfacet_handle_release(session->principal);
        free(session);
    }
    for (size_t i = 0; i < authority->user_count; i++) {
        AuthUser *user = &authority->users[i];
        if (user->authenticated_handle.platform != NULL)
            (void)libfacet_unexport_interface(user->authenticated_handle);
        if (user->human_handle.platform != NULL)
            (void)libfacet_unexport_interface(user->human_handle);
        if (user->principal_handle.platform != NULL)
            (void)libfacet_unexport_interface(user->principal_handle);
    }
    if (authority->security_handle.platform != NULL)
        (void)libfacet_unexport_interface(authority->security_handle);
    if (authority->service_handle.platform != NULL)
        (void)libfacet_unexport_interface(authority->service_handle);
    free(authority->users);
    memset(authority, 0, sizeof(*authority));
}

static int initialize_authority(AuthAuthority *authority,
                                const FacetSystemConfig *system,
                                const FacetConfigDomain *domain)
{
    authority->domain_id = domain->id;
    authority->user_count = system->user_count + domain->user_count;
    authority->users = calloc(authority->user_count, sizeof(*authority->users));
    if (authority->users == NULL) return -1;
    for (size_t i = 0; i < authority->user_count; i++) {
        authority->users[i].config = i < system->user_count
            ? &system->users[i] : &domain->users[i - system->user_count];
        authority->users[i].authority = authority;
    }
    authority->service.self = authority;
    authority->service.priv = authority;
    authority->service.getInterface = service_get_interface;
    authority->service.authenticate = authenticate;
    authority->security.self = authority;
    authority->security.priv = authority;
    authority->security.getInterface = security_get_interface;
    authority->security.create_session = security_create;
    return libfacet_export_interface(&authority->service,
                                     &IAuthService_MetaData,
                                     &authority->service_handle) == FACET_OK &&
           libfacet_export_interface(&authority->security,
                                     &ISecurityManager_MetaData,
                                     &authority->security_handle) == FACET_OK
        ? 0 : -1;
}

int dominit0_auth_initialize(Dominit0SystemConfig *system)
{
    if (system == NULL || authorities != NULL) return -1;
    authorities = calloc(system->domain_count, sizeof(*authorities));
    if (authorities == NULL) return -1;
    authority_count = system->domain_count;
    for (size_t i = 0; i < system->domain_count; i++) {
        const FacetConfigDomain *domain = &system->parsed.domains[i];
        if (domain->authentication_source_index == (size_t)-1) continue;
        AuthAuthority *authority = &authorities[i];
        if (initialize_authority(authority, &system->parsed, domain) != 0 ||
            dominit0_environment_bind_named(system->current_domains[i]->environment,
                                             "auth", IID_IAuthService,
                                             authority->service_handle) != 0 ||
            dominit0_environment_bind_named(system->current_domains[i]->environment,
                                             "security", IID_ISecurityManager,
                                             authority->security_handle) != 0) {
            dominit0_auth_destroy();
            return -1;
        }
    }
    return 0;
}

void dominit0_auth_destroy(void)
{
    for (size_t i = 0; i < authority_count; i++)
        destroy_authority(&authorities[i]);
    free(authorities);
    authorities = NULL;
    authority_count = 0;
}
