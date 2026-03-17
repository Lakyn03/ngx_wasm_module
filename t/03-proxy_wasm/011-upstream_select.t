# vim:set ft= ts=4 sts=4 sw=4 et fdm=marker:

use strict;
use lib '.';
use t::TestWasmX;

plan tests => 148;
run_tests();

__DATA__

=== TEST 1: duplicate wasm_upstream_select directive
--- wasm_modules: on_phases
--- http_config
    upstream test_upstream {
        server 127.0.0.1:8001;
        wasm_upstream_select;
        wasm_upstream_select;
    }
--- config
    location /t {
        proxy_pass http://test_upstream/;
    }
--- error_log eval
qr/\[emerg\] .*? "wasm_upstream_select" directive is duplicate/
--- no_error_log
[error]
[crit]
--- must_die



=== TEST 2: wasm_upstream_select - no wasm{} configuration block
--- main_config
--- http_config
    upstream test_upstream {
        server 127.0.0.1:8001;
        wasm_upstream_select;
    }
--- config
    location /t {
        proxy_pass http://test_upstream/;
    }
--- error_log eval
qr/\[emerg\] .*? "wasm_upstream_select" directive is specified but config has no "wasm" section/
--- no_error_log
[error]
[crit]
--- must_die



=== TEST 3: wasm_upstream_select outside upstream{} configuration block
--- wasm_modules: on_phases
--- config
    location /t {
        wasm_upstream_select;
        return 200;
    }
--- error_log eval
qr/\[emerg\] .*? "wasm_upstream_select" directive is not allowed here/
--- no_error_log
[error]
[crit]
--- must_die



=== TEST 4: wasm_upstream_select no plugin - fallback to round robin
--- load_nginx_modules: ngx_http_echo_module
--- wasm_modules: on_phases
--- http_config
    server {
        listen       8891;
        location / {
            echo "1";
        }
    }
    server {
        listen       8892;
        location / {
            echo "2";
        }
    }
    server {
        listen       8893;
        location / {
            echo "3";
        }
    }
    server {
        listen       8894;
        location / {
            echo "4";
        }
    }
    upstream test_upstream {
        server 127.0.0.1:8891;
        server 127.0.0.1:8892;
        server 127.0.0.1:8893;
        server 127.0.0.1:8894;
        wasm_upstream_select;
    }
--- config
    location /t {
        proxy_pass http://test_upstream/;
    }
--- request eval
["GET /t", "GET /t", "GET /t", "GET /t"]
--- response_body eval
["1\n", "2\n", "3\n", "4\n"]
--- error_log
[wasm] calling original get_peer
--- no_error_log
[error]
[crit]
[emerg]



=== TEST 5: plugin with no upstream_select - fallback to round robin
--- load_nginx_modules: ngx_http_echo_module
--- wasm_modules: instance_lifecycle
--- http_config
    server {
        listen       8891;
        location / {
            echo "1";
        }
    }
    server {
        listen       8892;
        location / {
            echo "2";
        }
    }
    server {
        listen       8893;
        location / {
            echo "3";
        }
    }
    server {
        listen       8894;
        location / {
            echo "4";
        }
    }
    upstream test_upstream {
        server 127.0.0.1:8891;
        server 127.0.0.1:8892;
        server 127.0.0.1:8893;
        server 127.0.0.1:8894;
        wasm_upstream_select;
    }
--- config
    location /t {
        proxy_wasm instance_lifecycle;
        proxy_pass http://test_upstream/;
    }
--- request eval
["GET /t", "GET /t", "GET /t", "GET /t"]
--- response_body eval
["1\n", "2\n", "3\n", "4\n"]
--- error_log
[wasm] calling original get_peer
--- no_error_log
[error]
[crit]
[emerg]



=== TEST 6: plugin does not choose upstream - fallback to round robin
--- load_nginx_modules: ngx_http_echo_module
--- wasm_modules: on_phases
--- http_config
    server {
        listen       8891;
        location / {
            echo "1";
        }
    }
    server {
        listen       8892;
        location / {
            echo "2";
        }
    }
    server {
        listen       8893;
        location / {
            echo "3";
        }
    }
    server {
        listen       8894;
        location / {
            echo "4";
        }
    }
    upstream test_upstream {
        server 127.0.0.1:8891;
        server 127.0.0.1:8892;
        server 127.0.0.1:8893;
        server 127.0.0.1:8894;
        wasm_upstream_select;
    }
--- config
    location /t {
        proxy_wasm on_phases;
        proxy_pass http://test_upstream/;
    }
--- request eval
["GET /t", "GET /t", "GET /t", "GET /t"]
--- response_body eval
["1\n", "2\n", "3\n", "4\n"]
--- error_log
on_upstream_select
[wasm] no upstream selected in "on_upstream_select"
[wasm] calling original get_peer
--- no_error_log
[error]
[crit]
[emerg]



=== TEST 7: set_upstream called in request_headers phase not allowed
--- load_nginx_modules: ngx_http_echo_module
--- wasm_modules: hostcalls
--- http_config
    server {
        listen       8891;
        location / {
            echo "ok";
        }
    }
    server {
        listen       8892;
        location / {
            echo "original";
        }
    }
    upstream test_upstream {
        server 127.0.0.1:8892;
        wasm_upstream_select;
    }
--- config
    location /t {
        proxy_wasm hostcalls 'test=/t/set_upstream on=request_headers ip=127.0.0.1 port=8891';
        proxy_pass http://test_upstream/;
    }
--- error_code: 500
--- error_log
host trap (bad usage): can only set upstream during "on_upstream_select"
--- no_error_log
[emerg]



=== TEST 8: set_upstream called in request_body phase not allowed
--- load_nginx_modules: ngx_http_echo_module
--- wasm_modules: hostcalls
--- http_config
    server {
        listen       8891;
        location / {
            echo "ok";
        }
    }
    server {
        listen       8892;
        location / {
            echo "original";
        }
    }
    upstream test_upstream {
        server 127.0.0.1:8892;
        wasm_upstream_select;
    }
--- config
    location /t {
        proxy_wasm hostcalls 'test=/t/set_upstream on=request_body ip=127.0.0.1 port=8891';
        proxy_pass http://test_upstream/;
    }
--- request
POST /t
hello
--- error_code: 500
--- error_log
host trap (bad usage): can only set upstream during "on_upstream_select"
--- no_error_log
[emerg]



=== TEST 9: set_upstream called in response_headers phase not allowed
--- load_nginx_modules: ngx_http_echo_module
--- wasm_modules: hostcalls
--- http_config
    server {
        listen       8891;
        location / {
            echo "ok";
        }
    }
    server {
        listen       8892;
        location / {
            echo "original";
        }
    }
    upstream test_upstream {
        server 127.0.0.1:8892;
        wasm_upstream_select;
    }
--- config
    location /t {
        proxy_wasm hostcalls 'test=/t/set_upstream on=response_headers ip=127.0.0.1 port=8891';
        proxy_pass http://test_upstream/;
    }
--- error_code: 200
--- response_body
original
--- error_log
host trap (bad usage): can only set upstream during "on_upstream_select"
--- no_error_log
[emerg]



=== TEST 10: set_upstream called in response_body phase not allowed
--- load_nginx_modules: ngx_http_echo_module
--- wasm_modules: hostcalls
--- http_config
    server {
        listen       8891;
        location / {
            echo "ok";
        }
    }
    server {
        listen       8892;
        location / {
            echo "original";
        }
    }
    upstream test_upstream {
        server 127.0.0.1:8892;
        wasm_upstream_select;
    }
--- config
    location /t {
        proxy_wasm hostcalls 'test=/t/set_upstream on=response_body ip=127.0.0.1 port=8891';
        proxy_pass http://test_upstream/;
    }
--- error_code: 200
--- response_body
original
--- error_log
host trap (bad usage): can only set upstream during "on_upstream_select"
--- no_error_log
[emerg]



=== TEST 11: set_upstream called in on_upstream_info phase not allowed
--- load_nginx_modules: ngx_http_echo_module
--- wasm_modules: hostcalls
--- http_config
    server {
        listen       8891;
        location / {
            echo "ok";
        }
    }
    server {
        listen       8892;
        location / {
            echo "original";
        }
    }
    upstream test_upstream {
        server 127.0.0.1:8892;
        wasm_upstream_select;
    }
--- config
    location /t {
        proxy_wasm hostcalls 'test=/t/set_upstream on=upstream_select,upstream_info ip=127.0.0.1 port=8891';
        proxy_pass http://test_upstream/;
    }
--- error_code: 200
--- response_body
ok
--- error_log
host trap (bad usage): can only set upstream during "on_upstream_select"
--- no_error_log
[emerg]



=== TEST 12: set_upstream in on_upstream_select sets upstream server
--- load_nginx_modules: ngx_http_echo_module
--- wasm_modules: hostcalls
--- http_config
    server {
        listen       8891;
        location / {
            echo "ok";
        }
    }
    upstream test_upstream {
        server 0.0.0.0;
        wasm_upstream_select;
    }
--- config
    location /t {
        proxy_wasm hostcalls 'test=/t/set_upstream on=upstream_select ip=127.0.0.1 port=8891';
        proxy_pass http://test_upstream/;
    }
--- response_body
ok
--- error_log
[wasm] set upstream peer "127.0.0.1:8891"
--- no_error_log
[error]
[crit]
[emerg]



=== TEST 13: set_upstream IPv6 sets upstream server
--- load_nginx_modules: ngx_http_echo_module
--- wasm_modules: hostcalls
--- http_config
    server {
        listen       [::1]:8891;
        location / {
            echo "ok";
        }
    }
    upstream test_upstream {
        server 0.0.0.0;
        wasm_upstream_select;
    }
--- config
    location /t {
        proxy_wasm hostcalls 'test=/t/set_upstream on=upstream_select ip=[::1] port=8891';
        proxy_pass http://test_upstream/;
    }
--- response_body
ok
--- error_log
[wasm] set upstream peer "[::1]:8891"
--- no_error_log
[error]
[crit]
[emerg]



=== TEST 14: set_upstream invalid port
--- load_nginx_modules: ngx_http_echo_module
--- wasm_modules: hostcalls
--- http_config
    upstream test_upstream {
        server 127.0.0.1:8892;
        wasm_upstream_select;
    }
--- config
    location /t {
        proxy_wasm hostcalls 'test=/t/set_upstream on=upstream_select ip=127.0.0.1 port=70000';
        proxy_pass http://test_upstream/;
    }
--- error_code: 500
--- error_log
[wasm] proxy_set_upstream: port out of range: 70000
--- no_error_log
[emerg]



=== TEST 15: set_upstream empty ip
--- load_nginx_modules: ngx_http_echo_module
--- wasm_modules: hostcalls
--- http_config
    upstream test_upstream {
        server 127.0.0.1:8892;
        wasm_upstream_select;
    }
--- config
    location /t {
        proxy_wasm hostcalls 'test=/t/set_upstream on=upstream_select ip= port=8891';
        proxy_pass http://test_upstream/;
    }
--- error_code: 500
--- error_log
[wasm] invalid upstream address ""
--- no_error_log
[emerg]



=== TEST 16: set_upstream hostname as ip
--- load_nginx_modules: ngx_http_echo_module
--- wasm_modules: hostcalls
--- http_config
    upstream test_upstream {
        server 127.0.0.1:8892;
        wasm_upstream_select;
    }
--- config
    location /t {
        proxy_wasm hostcalls 'test=/t/set_upstream on=upstream_select ip=example.com port=8891';
        proxy_pass http://test_upstream/;
    }
--- error_code: 500
--- error_log
[wasm] invalid upstream address "example.com"
--- no_error_log
[emerg]



=== TEST 17: set_upstream invalid characters in ip
--- load_nginx_modules: ngx_http_echo_module
--- wasm_modules: hostcalls
--- http_config
    upstream test_upstream {
        server 127.0.0.1:8892;
        wasm_upstream_select;
    }
--- config
    location /t {
        proxy_wasm hostcalls 'test=/t/set_upstream on=upstream_select ip=@!#$% port=8891';
        proxy_pass http://test_upstream/;
    }
--- error_code: 500
--- error_log
[wasm] invalid upstream address "@!#$%"
--- no_error_log
[emerg]



=== TEST 18: set_upstream called twice not allowed
--- load_nginx_modules: ngx_http_echo_module
--- wasm_modules: hostcalls
--- http_config
    upstream test_upstream {
        server 127.0.0.1:8892;
        wasm_upstream_select;
    }
--- config
    location /t {
        proxy_wasm hostcalls 'test=/t/set_upstream on=upstream_select ip=127.0.0.1 port=8891';
        proxy_wasm hostcalls 'test=/t/set_upstream on=upstream_select ip=127.0.0.1 port=8892';
        proxy_pass http://test_upstream/;
    }
--- error_code: 500
--- error_log
[wasm] upstream "127.0.0.1:8891" already set, overwriting not allowed
--- no_error_log
[emerg]



=== TEST 19: on_upstream_info state ok
--- load_nginx_modules: ngx_http_echo_module
--- wasm_modules: hostcalls
--- http_config
    server {
        listen       8891;
        location / {
            echo "ok";
        }
    }
    upstream test_upstream {
        server 0.0.0.0;
        wasm_upstream_select;
    }
--- config
    location /t {
        proxy_wasm hostcalls 'test=/t/set_upstream on=upstream_select ip=127.0.0.1 port=8891';
        proxy_pass http://test_upstream/;
    }
--- error_code: 200
--- response_body
ok
--- error_log
[wasm] set upstream peer "127.0.0.1:8891"
[hostcalls] on_upstream_info, last_state: Ok
--- no_error_log
[error]
[crit]
[emerg]



=== TEST 20: on_upstream_info state Failed
--- load_nginx_modules: ngx_http_echo_module
--- wasm_modules: hostcalls
--- http_config
    upstream test_upstream {
        server 0.0.0.0;
        wasm_upstream_select;
    }
--- config
    location /t {
        proxy_wasm hostcalls 'test=/t/set_upstream on=upstream_select ip=127.0.0.1 port=9999';
        proxy_pass http://test_upstream/;
    }
--- error_code: 502
--- error_log
[wasm] set upstream peer "127.0.0.1:9999"
[hostcalls] on_upstream_info, last_state: Failed
--- no_error_log
[emerg]
