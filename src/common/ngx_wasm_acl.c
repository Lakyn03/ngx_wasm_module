#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"

#include <ngx_wasm_acl.h>


ngx_wasm_acl_ctx_t *
ngx_wasm_acl_find_ctx(ngx_cycle_t *cycle, ngx_str_t *name)
{
    size_t i;
    ngx_wasm_acl_ctx_t    **acls;
    ngx_wasm_core_conf_t  *wcf;

    wcf = ngx_wasm_core_cycle_get_conf(cycle);
    if (wcf == NULL) {
        return NULL;
    }

    acls = wcf->acls.elts;

    for (i = 0; i < wcf->acls.nelts; i++) {
        if (acls[i]->name.len == name->len
            && ngx_strncmp(acls[i]->name.data, name->data, name->len) == 0)
        {
            return acls[i];
        }
    }

    return NULL;
}


ngx_int_t
ngx_wasm_acl_init(ngx_wasm_acl_t *acl, ngx_wasm_acl_ctx_t *ctx,
    ngx_pool_t *pool)
{
    acl->ctx = ctx;

    if (ngx_array_init(&acl->recorded_addrs, pool, 8, sizeof(ngx_cidr_t))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}


ngx_int_t
ngx_wasm_acl_check_addr(ngx_wasm_acl_t *acl, struct sockaddr *addr)
{
    if (ngx_cidr_match(addr, &acl->ctx->deny_addrs) == NGX_OK) {
        return NGX_DECLINED;
    }

    if (ngx_cidr_match(addr, &acl->ctx->allow_addrs) == NGX_OK) {
        return NGX_OK;
    }

    if (ngx_cidr_match(addr, &acl->recorded_addrs) == NGX_OK) {
        return NGX_OK;
    }

    return NGX_DECLINED;
}


static ngx_int_t
ngx_wasm_acl_host_list_match(ngx_array_t *list, ngx_str_t *host)
{
    size_t                i;
    ngx_wasm_acl_host_t  *entries;

    entries = list->elts;

    for (i = 0; i < list->nelts; i++) {
        if (entries[i].wildcard) {
            if (host->len > entries[i].name.len
                && ngx_strncasecmp(host->data + host->len - entries[i].name.len,
                                   entries[i].name.data,
                                   entries[i].name.len) == 0)
            {
                return NGX_OK;
            }

        } else {
            if (entries[i].name.len == host->len
                && ngx_strncasecmp(entries[i].name.data,
                                   host->data, host->len) == 0)
            {
                return NGX_OK;
            }
        }
    }

    return NGX_DECLINED;
}


ngx_int_t
ngx_wasm_acl_check_host(ngx_wasm_acl_t *acl, ngx_str_t *host)
{
    u_char          *p;
    in_addr_t        v4;
    ngx_str_t        name;
    ngx_sockaddr_t   sa;

    if (host->len > 0 && host->data[0] == '[') {
        p = ngx_strlchr(host->data, host->data + host->len, ']');
        if (p == NULL) {
            return NGX_DECLINED;
        }

        name.data = host->data + 1;
        name.len  = p - host->data - 1;

    } else {
        p = ngx_strlchr(host->data, host->data + host->len, ':');
        name.data = host->data;
        name.len  = p ? (size_t) (p - host->data) : host->len;
    }

    ngx_memzero(&sa, sizeof(ngx_sockaddr_t));

    v4 = ngx_inet_addr(name.data, name.len);
    if (v4 != INADDR_NONE) {
        sa.sockaddr_in.sin_family = AF_INET;
        sa.sockaddr_in.sin_addr.s_addr = v4;
        return ngx_wasm_acl_check_addr(acl, &sa.sockaddr);
    }

#if (NGX_HAVE_INET6)
    if (ngx_inet6_addr(name.data, name.len,
                       sa.sockaddr_in6.sin6_addr.s6_addr) == NGX_OK)
    {
        sa.sockaddr_in6.sin6_family = AF_INET6;
        return ngx_wasm_acl_check_addr(acl, &sa.sockaddr);
    }
#endif

    if (ngx_wasm_acl_host_list_match(&acl->ctx->deny_hosts, &name) == NGX_OK) {
        return NGX_DECLINED;
    }

    return ngx_wasm_acl_host_list_match(&acl->ctx->allow_hosts, &name);
}


static ngx_int_t
ngx_wasm_sockaddr_to_cidr(struct sockaddr *sa, ngx_cidr_t *cidr)
{
    switch (sa->sa_family) {

    case AF_INET:
        cidr->family = AF_INET;
        cidr->u.in.addr = ((struct sockaddr_in *) sa)->sin_addr.s_addr;
        cidr->u.in.mask = 0xffffffff;
        return NGX_OK;

#if (NGX_HAVE_INET6)
    case AF_INET6:
        cidr->family = AF_INET6;
        ngx_memcpy(cidr->u.in6.addr.s6_addr,
                   ((struct sockaddr_in6 *) sa)->sin6_addr.s6_addr, 16);
        ngx_memset(cidr->u.in6.mask.s6_addr, 0xff, 16);
        return NGX_OK;
#endif
    }

    return NGX_DECLINED;
}


ngx_int_t
ngx_wasm_acl_add_addr(ngx_wasm_acl_t *acl, struct sockaddr *addr)
{
    ngx_cidr_t  cidr, *slot;

    if (ngx_cidr_match(addr, &acl->ctx->deny_addrs) == NGX_OK) {
        return NGX_DECLINED;
    }

    if (ngx_wasm_sockaddr_to_cidr(addr, &cidr) != NGX_OK) {
        return NGX_ERROR;
    }

    slot = ngx_array_push(&acl->recorded_addrs);
    if (slot == NULL) {
        return NGX_ERROR;
    }

    *slot = cidr;

    return NGX_OK;
}
