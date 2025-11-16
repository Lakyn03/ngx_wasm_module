#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"

#include <ngx_http_wasm_upstream.h>


char *
ngx_http_wasm_upstream_select_directive(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_upstream_srv_conf_t  *uscf;

    uscf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_upstream_module);
    uscf->peer.init_upstream = ngx_http_wasm_upstream_init;

    return NGX_CONF_OK;
}


ngx_int_t
ngx_http_wasm_upstream_init(ngx_conf_t *cf, ngx_http_upstream_srv_conf_t *us)
{
    us->peer.init = ngx_http_wasm_upstream_init_peer;

    return NGX_OK;
}


ngx_int_t
ngx_http_wasm_upstream_init_peer(ngx_http_request_t *r,
    ngx_http_upstream_srv_conf_t *us)
{
    ngx_http_wasm_upstream_peer_data_t  *up;

    up = ngx_pcalloc(r->pool, sizeof(ngx_http_wasm_upstream_peer_data_t));
    if (up == NULL) {
        return NGX_ERROR;
    }

    up->request = r;

    r->upstream->peer.data = up;
    r->upstream->peer.get = ngx_http_wasm_upstream_get_peer;
    r->upstream->peer.free = ngx_http_wasm_upstream_free_peer;
    r->upstream->peer.notify = ngx_http_wasm_upstream_notify_peer;

    return NGX_OK;
}

// todo - placeholder, only for testing
ngx_int_t
ngx_http_wasm_upstream_get_peer(ngx_peer_connection_t *pc, void *data)
{
    ngx_http_wasm_upstream_peer_data_t  *up = data;
    ngx_url_t  url;
    ngx_str_t  addr = ngx_string("127.0.0.1:8001");
    u_char    *p;
    ngx_pool_t *pool = up->request->pool;

    ngx_memzero(&url, sizeof(ngx_url_t));

    p = ngx_pnalloc(pool, addr.len);
    if (p == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(p, addr.data, addr.len);

    url.url.data = p;
    url.url.len = addr.len;
    url.default_port = 8001;
    url.no_resolve = 1;
    url.uri_part = 0;

    if (ngx_parse_url(pool, &url) != NGX_OK) {
        return NGX_ERROR;
    }

    if (url.addrs == NULL || url.addrs[0].sockaddr == NULL) {
        return NGX_ERROR;
    }

    pc->sockaddr = url.addrs[0].sockaddr;
    pc->socklen = url.addrs[0].socklen;
    pc->name = &url.addrs[0].name;
    pc->cached = 0;
    pc->connection = NULL;

    return NGX_OK;
}


void
ngx_http_wasm_upstream_free_peer(ngx_peer_connection_t *pc, void *data,
    ngx_uint_t state)
{
    (void) pc;
    (void) data;
    (void) state;
}


void
ngx_http_wasm_upstream_notify_peer(ngx_peer_connection_t *pc, void *data,
    ngx_uint_t type)
{
    (void) pc;
    (void) data;
    (void) type;
}
