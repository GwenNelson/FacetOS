#include <facetos/dominit0/auth.h>
#include <facetos/dominit0/environment.h>
#include <facetos/sha256.h>
#include <facetos/interfaces/IAuthService.h>
#include <facetos/interfaces/IAuthenticatedPrincipal.h>
#include <facetos/interfaces/IPrincipal.h>
#include <string.h>
#include <stdlib.h>

typedef struct AuthUser { const FacetConfigUser *user; IPrincipal principal; IAuthenticatedPrincipal authenticated; FacetHandle principal_handle, authenticated_handle; } AuthUser;
static struct { IAuthService service; FacetHandle handle; AuthUser *users; size_t count; } auth;
static int eq(uuid_t a, uuid_t b) { return memcmp(a.bytes,b.bytes,sizeof(a.bytes))==0; }
static FacetResult ret(FacetHandle h, FacetHandle *o) { if(!o)return FACET_INVALID_ARGUMENT;*o=(FacetHandle){0};if(!h.platform)return FACET_INVALID_HANDLE;*o=h;return FACET_OK; }
static FacetResult principal_get(void *s,uuid_t iid,FacetHandle *o){AuthUser*u=s;if(eq(iid,IID_IGenericObject)||eq(iid,IID_IPrincipal))return ret(u->principal_handle,o);return FACET_NO_INTERFACE;}
static FacetResult principal_id(void*s,uuid_t*o){if(!o)return FACET_INVALID_ARGUMENT; AuthUser*u=s; memset(o,0,sizeof(*o)); memcpy(o->bytes,u->user->name,u->user->name[0]?1:0);return FACET_OK;}
static FacetResult principal_name(void*s,FacetString*o){if(!o)return FACET_INVALID_ARGUMENT;AuthUser*u=s;o->data=u->user->name;o->length=strlen(o->data);return FACET_OK;}
static FacetResult authenticated_get(void*s,uuid_t iid,FacetHandle*o){AuthUser*u=s;if(eq(iid,IID_IGenericObject)||eq(iid,IID_IAuthenticatedPrincipal))return ret(u->authenticated_handle,o);return FACET_NO_INTERFACE;}
static FacetResult authenticated_principal(void*s,FacetHandle*o){return ret(((AuthUser*)s)->principal_handle,o);}
static int export_user(AuthUser *u){if(u->authenticated_handle.platform)return 0;u->principal.self=u;u->principal.getInterface=principal_get;u->principal.getid=principal_id;u->principal.getname=principal_name;u->authenticated.self=u;u->authenticated.getInterface=authenticated_get;u->authenticated.get_principal=authenticated_principal;return libfacet_export_interface(&u->principal,&IPrincipal_MetaData,&u->principal_handle)==FACET_OK&&libfacet_export_interface(&u->authenticated,&IAuthenticatedPrincipal_MetaData,&u->authenticated_handle)==FACET_OK?0:-1;}
static FacetResult service_get(void*s,uuid_t iid,FacetHandle*o){(void)s;if(eq(iid,IID_IGenericObject)||eq(iid,IID_IAuthService))return ret(auth.handle,o);return FACET_NO_INTERFACE;}
static FacetResult authenticate(void*s,const FacetString*n,const FacetString*p,FacetHandle*o){(void)s;if(!n||!p||!n->data||!p->data||p->length>4096)return FACET_INVALID_ARGUMENT;uint8_t d[32];char h[65];facet_sha256((const uint8_t*)p->data,p->length,d);facet_sha256_hex(d,h);for(size_t i=0;i<auth.count;i++){AuthUser*u=&auth.users[i];if(strlen(u->user->name)==n->length&&!memcmp(u->user->name,n->data,n->length)&&memcmp(h,u->user->password_sha256,65)==0){if(export_user(u))return FACET_OUT_OF_MEMORY;return ret(u->authenticated_handle,o);}}if(o)*o=(FacetHandle){0};return FACET_ACCESS_DENIED;}
int dominit0_auth_initialize(Dominit0SystemConfig *system){if(!system||auth.handle.platform)return -1;auth.count=system->parsed.user_count;auth.users=calloc(auth.count,sizeof(*auth.users));if(!auth.users)return -1;for(size_t i=0;i<auth.count;i++)auth.users[i].user=&system->parsed.users[i];auth.service.self=&auth;auth.service.getInterface=service_get;auth.service.authenticate=authenticate;if(libfacet_export_interface(&auth.service,&IAuthService_MetaData,&auth.handle)!=FACET_OK)return -1;for(size_t i=0;i<system->domain_count;i++)if(system->parsed.domains[i].authentication_source_index!=(size_t)-1&&dominit0_environment_bind_named(system->current_domains[i]->environment,"auth",IID_IAuthService,auth.handle)!=0)return -1;return 0;}
void dominit0_auth_destroy(void){for(size_t i=0;i<auth.count;i++){if(auth.users[i].authenticated_handle.platform)libfacet_unexport_interface(auth.users[i].authenticated_handle);if(auth.users[i].principal_handle.platform)libfacet_unexport_interface(auth.users[i].principal_handle);}if(auth.handle.platform)libfacet_unexport_interface(auth.handle);free(auth.users);memset(&auth,0,sizeof(auth));}
