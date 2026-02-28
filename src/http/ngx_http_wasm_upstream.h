#ifndef NGINX_WASMX_INDEX_NGX_HTTP_WASM_UPSTREAM_H
#define NGINX_WASMX_INDEX_NGX_HTTP_WASM_UPSTREAM_H

#include <ngx_http_wasm.h>

typedef struct {
    ngx_http_request_t                 *request;

    struct sockaddr                    *sockaddr;
    socklen_t                           socklen;
    ngx_addr_t                         *local;

    ngx_str_t                           host;
    ngx_str_t                          *name;
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
void ngx_proxy_wasm_on_upstream_select(ngx_proxy_wasm_exec_t *pwexec);
ngx_int_t ngx_http_wasm_set_upstream(ngx_http_wasm_upstream_peer_data_t *up,
    ngx_str_t *addr, ngx_int_t port, ngx_pool_t *pool);

#endif //NGINX_WASMX_INDEX_NGX_HTTP_WASM_UPSTREAM_H
