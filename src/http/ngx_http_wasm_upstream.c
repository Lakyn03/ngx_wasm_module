#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"

#include <ngx_http_wasm_upstream.h>


char *
ngx_http_wasm_upstream_select_directive(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_wavm_t                    *vm;
    ngx_http_wasm_srv_conf_t      *wscf = conf;
    ngx_http_upstream_srv_conf_t  *uscf;

    uscf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_upstream_module);

    vm = ngx_wasm_main_vm(cf->cycle);
    if (vm == NULL) {
        return NGX_WASM_CONF_ERR_NO_WASM;
    }

    if (wscf->original_init_upstream) {
        return "is duplicate";
    }

    if (uscf->peer.init_upstream) {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                           "load balancing method redefined");
    }

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
    ngx_http_upstream_t                 *u;
    ngx_http_wasm_req_ctx_t             *rctx;
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

    u = r->upstream;

    if (u->conf->next_upstream_tries) {
        u->peer.tries = u->conf->next_upstream_tries;
    }

    up->data = u->peer.data;
    up->original_get_peer = u->peer.get;
    up->original_free_peer = u->peer.free;

    up->request = r;

    u->peer.data = up;
    u->peer.get = ngx_http_wasm_upstream_get_peer;
    u->peer.free = ngx_http_wasm_upstream_free_peer;
    u->peer.notify = ngx_http_wasm_upstream_notify_peer;
    u->peer.test_next = ngx_http_wasm_upstream_test_next;

    if (ngx_http_wasm_rctx(r, &rctx) == NGX_OK) {
        rctx->upstream_peer = up;
    }

    return NGX_OK;
}


ngx_int_t
ngx_http_wasm_upstream_get_peer(ngx_peer_connection_t *pc, void *data)
{
    void                                *peer_data;
    ngx_int_t                            rc = NGX_ERROR;
    ngx_chain_t                         *cl, *next;
    ngx_http_request_t                  *r;
    ngx_http_wasm_req_ctx_t             *rctx;
    ngx_http_wasm_upstream_peer_data_t  *up = data;

    r = up->request;

    rc = ngx_http_wasm_rctx(r, &rctx);
    if (rc == NGX_DECLINED) {
        goto original;
    } else if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    rctx->req_headers_modified = 0;
    peer_data = r->upstream->peer.data;
    r->upstream->peer.data = data;

    rc = ngx_proxy_wasm_upstream_resume(rctx, NGX_PROXY_WASM_STEP_UPSTREAM_SELECT);
    r->upstream->peer.data = peer_data;
    if (rc == NGX_ERROR || rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return NGX_ERROR;
    }

    if (rctx->local_resp_status) {
        ngx_http_wasm_flush_local_response(rctx);

        if (r->header_sent) {
            r->upstream->header_sent = 1;
        }

        return NGX_ERROR;
    }

    if (rctx->req_headers_modified) {
        cl = r->upstream->request_bufs;

        if (cl && (!r->request_body || cl != r->request_body->bufs)) {
            ngx_pfree(r->pool, cl->buf->start);
        }

        for (cl = r->upstream->request_bufs; cl; /* void */ ) {
            next = cl->next;
            ngx_free_chain(r->pool, cl);
            cl = next;
        }

        /* reset upstream buffers */
        r->upstream->request_bufs = r->request_body
                                    ? r->request_body->bufs : NULL;

        if (r->upstream->create_request(r) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    if (up->sockaddr && up->socklen) {
        pc->sockaddr = up->sockaddr;
        pc->socklen = up->socklen;
        pc->name = up->name;
        pc->cached = 0;
        pc->connection = NULL;

        return NGX_OK;
    }

    ngx_wasm_log_error(NGX_LOG_INFO, r->connection->log, 0,
                       "no upstream selected in \"on_upstream_select\"");

original:

    ngx_wasm_log_error(NGX_LOG_INFO, r->connection->log, 0,
                   "calling original get_peer");
    return up->original_get_peer(pc, up->data);
}


void
ngx_http_wasm_upstream_free_peer(ngx_peer_connection_t *pc, void *data,
    ngx_uint_t state)
{
    void                               *peer_data;
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

        peer_data = r->upstream->peer.data;
        r->upstream->peer.data = data;

        ngx_proxy_wasm_upstream_resume(rctx, NGX_PROXY_WASM_STEP_UPSTREAM_INFO);

        r->upstream->peer.data = peer_data;

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
    ngx_http_wasm_upstream_peer_data_t  *up;

    r = pc->connection->data;
    rc = ngx_http_wasm_rctx(r, &rctx);
    if (rc != NGX_OK) {
        return NGX_OK;
    }

    up = rctx->upstream_peer;
    if (up == NULL) {
        return NGX_OK;
    }

    if (up->sockaddr && up->socklen) {
        up->accept_resp = 0;
        up->last_status = status;

        r->upstream->peer.data = up;

        ngx_proxy_wasm_upstream_resume(rctx, NGX_PROXY_WASM_STEP_UPSTREAM_SPECIAL_RESPONSE);

        r->upstream->peer.data = data;

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
        ngx_wasm_log_error(NGX_LOG_DEBUG, up->request->connection->log, 0,
                       "upstream \"%V\" already set, overwriting not allowed",
                           up->name);
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

    if (ngx_parse_url(pool, &url) != NGX_OK
        || url.addrs == NULL
        || url.addrs[0].sockaddr == NULL)
    {
        ngx_wasm_log_error(NGX_LOG_DEBUG, up->request->connection->log, 0,
                       "invalid upstream address \"%V\"", addr);
        return NGX_DECLINED;
    }

    ngx_wasm_log_error(NGX_LOG_DEBUG, up->request->connection->log, 0,
                   "set upstream peer \"%V\"", &url.addrs[0].name);

    up->sockaddr = url.addrs[0].sockaddr;
    up->socklen = url.addrs[0].socklen;
    up->name = &url.addrs[0].name;

    return NGX_OK;
}


ngx_int_t
ngx_http_wasm_get_last_upstream_state(ngx_proxy_wasm_ctx_t *pwctx,
    ngx_http_upstream_state_t **state)
{
    ngx_http_request_t       *r;
    ngx_http_wasm_req_ctx_t  *rctx;

    if (pwctx->step != NGX_PROXY_WASM_STEP_UPSTREAM_INFO) {
        return NGX_DECLINED;
    }

    rctx = pwctx->data;
    r = rctx->r;

    if (r->upstream_states == NULL || r->upstream_states->nelts == 0) {
        return NGX_DECLINED;
    }

    *state = (ngx_http_upstream_state_t *) r->upstream_states->elts
             + (r->upstream_states->nelts - 1);
    return NGX_OK;
}
