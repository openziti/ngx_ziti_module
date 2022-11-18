#ifndef NGX_ZITI_MODULE_H
#define NGX_ZITI_MODULE_H

#include <ngx_auto_config.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_module.h>
#include <ngx_http.h>
#include <ngx_log.h>
#include <ngx_conf_file.h>
#include <ngx_thread_pool.h>
#include <ziti/zitilib.h>
#include <ziti/ziti.h>

extern ngx_module_t ngx_ziti_module;

#define NGX_ZITI_CONF 0x80000001
#define NGX_ZITI_BIND_CONF 0x80000002

typedef struct {
    ngx_str_t name;

    ngx_str_t identity_file; //file path to an identity file, used to initialize `ztx`
    ngx_array_t* services; //array of ngx_str_t for service names
    ngx_array_t* upstreams; //array of ngx_str_t upstream targets, corresponds to `services`

    u_int num_services;
    ziti_context ztx; //ziti context, maps to 1 `ziti` block
} ngx_ziti_block_conf_t;

typedef struct {
    //an array of blocks already parsed
    ngx_array_t* blocks;

    //temp storage for use during configuration parsing
    ngx_ziti_block_conf_t* cur_block;
} ngx_ziti_conf_t;

typedef struct {
    ngx_ziti_block_conf_t* block;
    ngx_str_t service;
    ngx_str_t upstream;
    ngx_cycle_t *cycle;
} ngx_ziti_service_ctx_t;

typedef struct {
    ziti_socket_t client_socket;
    ngx_str_t upstream;
    char client_name[128];
} ngx_ziti_service_client_ctx_t;

static char *ngx_ziti(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_ziti_identity_file(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char* ngx_ziti_bind(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char* ngx_ziti_bind_upstream(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static void* ngx_ziti_create_main_conf(ngx_cycle_t *cycle);
static char* ngx_ziti_init_main_conf(ngx_cycle_t *cycle, void *conf);

static ngx_int_t ngx_ziti_init_process(ngx_cycle_t *cycle);

ngx_int_t ngx_ziti_run_service_offload(ngx_cycle_t* cycle, ngx_ziti_service_ctx_t service_ctx);
static void ngx_ziti_run_service(void *data, ngx_log_t *log);
static void ngx_ziti_run_service_client(void *data, ngx_log_t *log);
static void ngx_ziti_run_service_client_complete(ngx_event_t *ev);

#endif //NGX_ZITI_MODULE_H
