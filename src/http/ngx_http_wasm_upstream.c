#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"

#include <ngx_http_wasm_upstream.h>


char *
ngx_http_wasm_upstream_select_directive(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_wasm_srv_conf_t      *wscf = conf;
    ngx_http_upstream_srv_conf_t  *uscf;

    uscf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_upstream_module);

    wscf->original_init_upstream = uscf->peer.init_upstream
                                   ? uscf->peer.init_upstream
                                   : ngx_http_upstream_init_round_robin;

    uscf->peer.init_upstream = ngx_http_wasm_upstream_init;

    return NGX_CONF_OK;
}


ngx_int_t
ngx_http_wasm_upstream_init(ngx_conf_t *cf, ngx_http_upstream_srv_conf_t *us)
{
    ngx_http_wasm_srv_conf_t  *wscf;

    wscf = ngx_http_conf_upstream_srv_conf(us, ngx_http_wasm_module);

    if (wscf->original_init_upstream(cf, us) != NGX_OK) {
        return NGX_ERROR;
    }

    wscf->original_init_peer = us->peer.init;
    us->peer.init = ngx_http_wasm_upstream_init_peer;

    return NGX_OK;
}


ngx_int_t
ngx_http_wasm_upstream_init_peer(ngx_http_request_t *r,
    ngx_http_upstream_srv_conf_t *us)
{
    ngx_http_wasm_srv_conf_t            *wscf;
    ngx_http_wasm_upstream_peer_data_t  *up;

    wscf = ngx_http_conf_upstream_srv_conf(us, ngx_http_wasm_module);

    up = ngx_pcalloc(r->pool, sizeof(ngx_http_wasm_upstream_peer_data_t));
    if (up == NULL) {
        return NGX_ERROR;
    }

    if (wscf->original_init_peer(r, us) != NGX_OK) {
        return NGX_ERROR;
    }

    up->data = r->upstream->peer.data;
    up->original_get_peer = r->upstream->peer.get;
    up->original_free_peer = r->upstream->peer.free;

    up->request = r;

    r->upstream->peer.data = up;
    r->upstream->peer.get = ngx_http_wasm_upstream_get_peer;
    r->upstream->peer.free = ngx_http_wasm_upstream_free_peer;
    r->upstream->peer.notify = ngx_http_wasm_upstream_notify_peer;
    r->upstream->peer.test_next = ngx_http_wasm_upstream_test_next;

    return NGX_OK;
}


ngx_int_t
ngx_http_wasm_upstream_get_peer(ngx_peer_connection_t *pc, void *data)
{
    ngx_int_t                            rc = NGX_ERROR;
    ngx_http_request_t                  *r;
    ngx_http_wasm_req_ctx_t             *rctx;
    ngx_http_wasm_upstream_peer_data_t  *up = data;

    r = up->request;

    rc = ngx_http_wasm_rctx(r, &rctx);
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    rc = ngx_proxy_wasm_upstream_resume(rctx, NGX_PROXY_WASM_STEP_UPSTREAM_SELECT);
    if (rc == NGX_ERROR) {
        return NGX_ERROR;
    }

    if (rctx->local_resp_status) {
        ngx_http_wasm_flush_local_response(rctx);

        if (r->header_sent) {
            r->upstream->header_sent = 1;
        }

        return NGX_ERROR;
    }

    if (up->sockaddr && up->socklen) {
        pc->sockaddr = up->sockaddr;
        pc->socklen = up->socklen;
        pc->name = up->name;
        pc->cached = 0;
        pc->connection = NULL;

        return NGX_OK;
    }

    return up->original_get_peer(pc, up->data);
}


void
ngx_http_wasm_upstream_free_peer(ngx_peer_connection_t *pc, void *data,
    ngx_uint_t state)
{
    ngx_int_t                           rc;
    ngx_http_request_t                  *r;
    ngx_http_wasm_req_ctx_t             *rctx;
    ngx_http_wasm_upstream_peer_data_t  *up = data;

    if (up->sockaddr && up->socklen) {
        r = up->request;
        rc = ngx_http_wasm_rctx(r, &rctx);
        if (rc != NGX_OK) {
            return;
        }

        up->last_peer_state = state;
        if (pc->tries) {
            pc->tries--;
        }

        up->sockaddr = NULL;
        up->socklen = 0;

        ngx_proxy_wasm_upstream_resume(rctx, NGX_PROXY_WASM_STEP_UPSTREAM_INFO);

        return;
    }

    up->original_free_peer(pc, up->data, state);
}


void
ngx_http_wasm_upstream_notify_peer(ngx_peer_connection_t *pc, void *data,
    ngx_uint_t type)
{
    (void) pc;
    (void) data;
    (void) type;
}


ngx_int_t
ngx_http_wasm_upstream_test_next(ngx_peer_connection_t *pc, void *data,
    ngx_uint_t status)
{
    ngx_int_t                           rc;
    ngx_http_request_t                  *r;
    ngx_http_wasm_req_ctx_t             *rctx;
    ngx_http_wasm_upstream_peer_data_t  *up = data;

    if (up->sockaddr && up->socklen) {
        r = up->request;
        rc = ngx_http_wasm_rctx(r, &rctx);
        if (rc != NGX_OK) {
            return NGX_OK;
        }

        up->accept_resp = 0;
        up->last_status = status;

        ngx_proxy_wasm_upstream_resume(rctx, NGX_PROXY_WASM_STEP_UPSTREAM_SPECIAL_RESPONSE);

        if (up->accept_resp) {
            return NGX_DECLINED;
        }
    }

    return NGX_OK;
}


ngx_int_t
ngx_proxy_wasm_upstream_resume(ngx_http_wasm_req_ctx_t *rctx, ngx_proxy_wasm_step_e step)
{
    ngx_int_t              rc;
    ngx_proxy_wasm_ctx_t  *pwctx;
    ngx_proxy_wasm_step_e  prev_step, last_completed_step;

    pwctx = rctx->data;
    if (pwctx == NULL) {
        return NGX_OK;
    }

    prev_step = pwctx->step;
    last_completed_step = pwctx->last_completed_step;
    pwctx->phase = ngx_wasm_phase_lookup(&ngx_http_wasm_subsystem,
                                 NGX_WASM_BACKGROUND_PHASE);

    rc = ngx_proxy_wasm_resume(pwctx, pwctx->phase, step);

    pwctx->step = prev_step;
    pwctx->last_completed_step = last_completed_step;

    return rc;
}


ngx_int_t
ngx_http_wasm_set_upstream(ngx_http_wasm_upstream_peer_data_t  *up,
    ngx_str_t *addr, ngx_int_t port, ngx_pool_t *pool)
{
    u_char    *p;
    ngx_url_t  url;

    if (up->sockaddr && up->socklen) {
        return NGX_DECLINED;
    }

    ngx_memzero(&url, sizeof(ngx_url_t));

    p = ngx_pnalloc(pool, addr->len);
    if (p == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(p, addr->data, addr->len);

    url.url.data = p;
    url.url.len = addr->len;
    url.default_port = port;
    url.no_resolve = 1;
    url.uri_part = 0;

    if (ngx_parse_url(pool, &url) != NGX_OK) {
        return NGX_DECLINED;
    }

    if (url.addrs == NULL || url.addrs[0].sockaddr == NULL) {
        return NGX_ERROR;
    }

    up->sockaddr = url.addrs[0].sockaddr;
    up->socklen = url.addrs[0].socklen;
    up->name = &url.addrs[0].name;

    return NGX_OK;
}
