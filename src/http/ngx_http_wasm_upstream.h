#ifndef _NGX_HTTP_WASM_UPSTREAM_H_INCLUDED_
#define _NGX_HTTP_WASM_UPSTREAM_H_INCLUDED_

#include <ngx_http_wasm.h>
#include <ngx_http_proxy_wasm.h>

typedef struct {
    ngx_http_request_t                 *request;

    void                               *data;
    ngx_event_get_peer_pt               original_get_peer;
    ngx_event_free_peer_pt              original_free_peer;

    ngx_http_upstream_conf_t           *original_conf;

    struct sockaddr                    *sockaddr;
    socklen_t                           socklen;
    ngx_addr_t                         *local;

    ngx_str_t                           host;
    ngx_str_t                          *name;

    ngx_uint_t                          last_peer_state;
    ngx_uint_t                          last_status;
    unsigned                            accept_resp:1;
} ngx_http_wasm_upstream_peer_data_t;


char *ngx_http_wasm_upstream_select_directive(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
ngx_int_t ngx_http_wasm_upstream_init(ngx_conf_t *cf,
    ngx_http_upstream_srv_conf_t *us);
ngx_int_t ngx_http_wasm_upstream_init_peer(ngx_http_request_t *r,
    ngx_http_upstream_srv_conf_t *us);
ngx_int_t ngx_http_wasm_upstream_get_peer(ngx_peer_connection_t *pc,
    void *data);
void ngx_http_wasm_upstream_free_peer(ngx_peer_connection_t *pc,
    void *data, ngx_uint_t state);
void ngx_http_wasm_upstream_notify_peer(ngx_peer_connection_t *pc,
    void *data, ngx_uint_t type);
ngx_int_t ngx_http_wasm_upstream_test_next(ngx_peer_connection_t *pc,
    void *data, ngx_uint_t status);
ngx_int_t ngx_proxy_wasm_upstream_resume(ngx_http_wasm_req_ctx_t *rctx, ngx_proxy_wasm_step_e step);
ngx_int_t ngx_http_wasm_set_upstream(ngx_http_wasm_upstream_peer_data_t *up,
    ngx_str_t *addr, ngx_int_t port, ngx_pool_t *pool);
ngx_int_t ngx_http_wasm_get_last_upstream_state(ngx_proxy_wasm_ctx_t *pwctx,
    ngx_http_upstream_state_t **state);
ngx_int_t ngx_http_wasm_set_upstream_timeouts(ngx_http_request_t *r, ngx_msec_t connect,
    ngx_msec_t send, ngx_msec_t read);
ngx_int_t ngx_http_wasm_get_upstreams(ngx_proxy_wasm_exec_t *pwexec, u_char **start, size_t *len);

#endif //_NGX_HTTP_WASM_UPSTREAM_H_INCLUDED_
