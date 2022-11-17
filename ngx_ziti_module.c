#include "ngx_ziti_module.h"


#ifndef NGX_THREADS
#error ngx_ziti_module.c requires --with-threads
#endif /* NGX_THREADS */

static ngx_str_t ngx_ziti_thread_pool_name = ngx_string("ngx_ziti_tp");

/*
 * nginx required symbols, unused in this project
 */
ngx_module_t *ngx_modules[] = {
        &ngx_ziti_module,
        NULL
};

char *ngx_module_names[] = {
        "ngx_ziti_module",
        NULL
};

char *ngx_module_order[] = {
        NULL
};

/*
 * Defines nginx module context
 */
static ngx_core_module_t ngx_ziti_module_ctx = {
        .name = ngx_string("ngx_ziti_module"),
        ngx_ziti_create_main_conf, /* preconfiguration */
        ngx_ziti_init_main_conf, /* postconfiguration */
};


static void* ngx_ziti_create_main_conf(ngx_cycle_t *cycle) {
    ngx_ziti_conf_t* ziti_conf = ngx_palloc(cycle->pool, sizeof(ngx_ziti_conf_t));
    if (ziti_conf == NULL) {
        return NGX_CONF_ERROR;
    }

    ziti_conf->blocks = ngx_array_create(cycle->pool, 1, sizeof(ngx_array_t));

    return ziti_conf;
}

static ngx_ziti_block_conf_t * ngx_ziti_new_block(ngx_conf_t *cf, ngx_str_t block_name) {
    ngx_ziti_block_conf_t* ziti_block;

    ziti_block = ngx_pcalloc(cf->pool, sizeof(ngx_ziti_block_conf_t));

    if(ziti_block == NULL) {
        return NULL;
    }

    ziti_block->name = block_name;

    ziti_block->services = ngx_array_create(cf->pool, 5, sizeof(ngx_str_t));

    if(ziti_block->services == NULL) {
        return NULL;
    }

    ziti_block->upstreams = ngx_array_create(cf->pool, 5, sizeof(ngx_str_t));

    if(ziti_block->upstreams == NULL) {
        return NULL;
    }

    return ziti_block;
}

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

static int hostname_to_ip(char * hostname , char* ip){
    struct hostent *he;
    struct in_addr **addr_list;
    int i = 0;

    if ( (he = gethostbyname( hostname ) ) == NULL){
        // get the host info
        herror("gethostbyname");
        return 1;
    }

    addr_list = (struct in_addr **) he->h_addr_list;

    if(addr_list[i] != NULL){
        //Return the first one;
        struct in_addr addr = *addr_list[i];
        strcpy(ip , inet_ntoa(addr) );
        return 0;
    }

    return 1;
}

static void ngx_ziti_run_service_client(void *data, ngx_log_t *log){
    ngx_ziti_service_client_ctx_t*  client_ctx = data;
    int upstream_socket, activity;
    struct sockaddr_in upstream_addr_info;
    fd_set socket_fd_reads;

    char read_buff[8*1024];

    char* upstream = malloc(client_ctx->upstream.len);
    strncpy(upstream, (char*) client_ctx->upstream.data, client_ctx->upstream.len);

    char* hostname_str = strsep(&upstream, ":");
    char* port_str = strsep(&upstream, ":");

    if(hostname_str == NULL) {
        ngx_log_error(NGX_LOG_EMERG, log, 0, "upstream socket creation failed: hostname_str was null");
        return;
    }

    if(port_str == NULL) {
        ngx_log_error(NGX_LOG_EMERG, log, 0, "upstream socket creation failed: port was null");
        return;
    }

    char hostname[2*1024];

    hostname_to_ip(hostname_str, (char*)&hostname);

    int port = atoi(port_str);

    if(port == 0 ){
        ngx_log_error(NGX_LOG_EMERG, log, 0, "upstream socket creation failed: port must be an integer (%s)", port_str);
        return;
    }

    // socket create and verification
    upstream_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (upstream_socket <= 0) {
        ngx_log_error(NGX_LOG_EMERG, log, 0, "upstream socket creation failed");
        return;
    }

    bzero(&upstream_addr_info, sizeof(upstream_addr_info));

    // assign IP, PORT
    upstream_addr_info.sin_family = AF_INET;
    upstream_addr_info.sin_addr.s_addr = inet_addr(hostname);
    upstream_addr_info.sin_port = htons(port);

    // connect the client_socket socket to server socket
    if (connect(upstream_socket, &upstream_addr_info, sizeof(upstream_addr_info)) != 0) {
        ngx_log_error(NGX_LOG_EMERG, log, 0, "connection with the upstream server failed");
        return;
    }

    //clear the socket set
    FD_ZERO(&socket_fd_reads);
    FD_SET(client_ctx->client_socket, &socket_fd_reads);
    FD_SET(upstream_socket, &socket_fd_reads);

    int max_fd = upstream_socket;

    if(client_ctx->client_socket > upstream_socket) {
        max_fd = client_ctx->client_socket;
    }

    do {
        FD_ZERO(&socket_fd_reads);
        FD_SET(client_ctx->client_socket, &socket_fd_reads);
        FD_SET(upstream_socket, &socket_fd_reads);

        activity = select(max_fd+1, &socket_fd_reads, NULL, NULL, NULL);

        if (activity < 0 && errno != EINTR) {
            ngx_log_error(NGX_LOG_EMERG, log, 0, "select error(%d)", activity);
            break;
        }

        //activity from client
        if (FD_ISSET(client_ctx->client_socket, &socket_fd_reads)) {
            ssize_t read_size = read(client_ctx->client_socket, read_buff, sizeof(read_buff));

            if(read_size > 0){
                printf("--writing to upstream\n");
                ssize_t written = write(upstream_socket, read_buff, read_size);
                printf("--wrote %zd\n", written);
            } else {
                printf("closing, client disconnected\n");
                break;
            }

            continue;
        }

        if (FD_ISSET(upstream_socket, &socket_fd_reads)) {
            ssize_t read_size = read(upstream_socket, read_buff, sizeof(read_buff));

            if(read_size > 0) {
                printf("--writing to client\n");
                ssize_t written = write(client_ctx->client_socket, read_buff, read_size);
                printf("--wrote %zd\n", written);
            } else {
                printf("closing, upstream disconnected\n");
                break;
            }
            continue;
        }
    } while(1);


    printf("service client thread exited\n");


    ssize_t read_size = read(upstream_socket, read_buff, sizeof(read_buff));

    if(read_size > 0) {
        printf("--writing to client: %s\n", read_buff);
    }

    close(upstream_socket);
    Ziti_close(client_ctx->client_socket);
    free(upstream);
}
static void ngx_ziti_run_service_client_complete(ngx_event_t *ev){
    //required or panic
}

static void ngx_ziti_run_service(void *data, ngx_log_t *log) {
    ngx_ziti_service_ctx_t *service_ctx = data;
    ziti_socket_t server_socket, new_client;
    char new_client_name[128];

    ngx_thread_task_t  *task;
    ngx_thread_pool_t* tp = ngx_thread_pool_get(service_ctx->cycle, &ngx_ziti_thread_pool_name);
    ngx_ziti_service_client_ctx_t * new_client_ctx;

    if(tp == NULL) {
        return;
    }

    server_socket = Ziti_socket(SOCK_STREAM);

    if(server_socket <= 0) {
        ngx_log_error(NGX_LOG_EMERG, log, 0, "for block %s service %s could not open server socket (%d), service thread exiting", service_ctx->block->name.data, service_ctx->service.data, server_socket);
        return;
    }

    int err = Ziti_bind(server_socket, service_ctx->block->ztx, (char*)service_ctx->service.data, NULL);

    if(err != ZITI_OK){
        ngx_log_error(NGX_LOG_EMERG, log, 0, "for block %s service %s could not bind server socket (%d), service thread exiting", service_ctx->block->name.data, service_ctx->service.data, err);
        return;
    }

    err = Ziti_listen(server_socket, 64);

    if(err != ZITI_OK){
        ngx_log_error(NGX_LOG_EMERG, log, 0, "for block %s service %s could not listen on server socket (%d), service thread exiting", service_ctx->block->name.data, service_ctx->service.data, err);
        return;
    }

    do {
         new_client = Ziti_accept(server_socket, new_client_name, sizeof(new_client_name));

         if(new_client <= 0){
             ngx_log_error(NGX_LOG_EMERG, log, 0, "for block %s service %s error returned from accept, exiting", service_ctx->block->name.data, service_ctx->service.data, server_socket);
             break;
         }

         //new child, start a thread for them
        task = ngx_thread_task_alloc(service_ctx->cycle->pool, sizeof(ngx_ziti_service_client_ctx_t));

        if (task == NULL) {
            ngx_log_error(NGX_LOG_EMERG, log, 0, "for block %s service %s could not create new thread task", service_ctx->block->name.data, service_ctx->service.data);
            Ziti_close(new_client);
            continue;
        }

        new_client_ctx = task->ctx;
        new_client_ctx->client_socket = new_client;
        new_client_ctx->upstream = service_ctx->upstream;
        strncpy(new_client_ctx->client_name, new_client_name, sizeof(new_client_name));

        task->handler = ngx_ziti_run_service_client;
        task->event.handler = ngx_ziti_run_service_client_complete;
        task->event.data = new_client_ctx;

        if (ngx_thread_task_post(tp, task) != NGX_OK) {
            ngx_log_error(NGX_LOG_EMERG, log, 0, "for block %s service %s could not start new client thread task", service_ctx->block->name.data, service_ctx->service.data);
            Ziti_close(new_client);
            continue;
        }

    }while(1);

    Ziti_close(server_socket);
}

void ngx_ziti_run_service_complete(ngx_event_t *ev){
    //required or panic
}

ngx_int_t ngx_ziti_run_service_offload(ngx_cycle_t* cycle, ngx_ziti_service_ctx_t service_ctx)
{
    ngx_ziti_service_ctx_t* new_ctx;
    ngx_thread_task_t  *task;
    ngx_thread_pool_t* tp = ngx_thread_pool_get(cycle, &ngx_ziti_thread_pool_name);

    if(tp == NULL) {
        return NGX_ERROR;
    }

    task = ngx_thread_task_alloc(cycle->pool, sizeof(ngx_ziti_service_ctx_t));
    if (task == NULL) {
        return NGX_ERROR;
    }

    new_ctx = task->ctx;
    new_ctx->upstream = service_ctx.upstream;
    new_ctx->block = service_ctx.block;
    new_ctx->service = service_ctx.service;
    new_ctx->cycle = cycle;

    task->handler = ngx_ziti_run_service;
    task->event.handler = ngx_ziti_run_service_complete;
    task->event.data = new_ctx;

    if (ngx_thread_task_post(tp, task) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}

static char* ngx_ziti_init_main_conf(ngx_cycle_t *cycle, void *conf) {
    ngx_ziti_conf_t* ziti_conf = conf;

    ngx_log_error(NGX_LOG_WARN, cycle->log, 0, "found %d ziti blocks", ziti_conf->blocks->nelts);

    if(ziti_conf->blocks->nelts > 0){
        Ziti_lib_init();
    }

    return NGX_OK;
}

/**
 * Defines nginx directives for the ziti block and sub components.
 */
static ngx_command_t ngx_ziti_commands[] = {

        {ngx_string("ziti"), /* directive */
         NGX_MAIN_CONF| NGX_DIRECT_CONF | NGX_CONF_BLOCK | NGX_CONF_TAKE1, /* location context and takes
                                            no arguments*/
         ngx_ziti, /* configuration setup function */
         0, /* No offset. Only one context is supported. */
         0, /* No offset when storing the module configuration on struct. */
         NULL},

        {ngx_string("ziti_identity_file"),
         NGX_ZITI_CONF | NGX_DIRECT_CONF | NGX_CONF_TAKE1,
         ngx_ziti_identity_file,
         0,
         0,
         NULL},
        { ngx_string("ziti_bind"),
          NGX_ZITI_CONF | NGX_DIRECT_CONF | NGX_CONF_BLOCK | NGX_CONF_TAKE1,
          ngx_ziti_bind,
          0,
          0,
          NULL
        },
        { ngx_string("ziti_upstream"),
          NGX_ZITI_BIND_CONF | NGX_DIRECT_CONF | NGX_CONF_TAKE1,
          ngx_ziti_bind_upstream,
          0,
          0,
          NULL
        },
        ngx_null_command /* command termination */
};

static ngx_int_t ngx_ziti_init_process(ngx_cycle_t *cycle){
    ngx_ziti_conf_t* ziti_conf = (ngx_ziti_conf_t*) ngx_get_conf(cycle->conf_ctx, ngx_ziti_module);

    ngx_ziti_block_conf_t **blocks = ziti_conf->blocks->elts;

    for(ngx_uint_t i = 0; i < ziti_conf->blocks->nelts; i++) {
        ngx_ziti_block_conf_t* block = blocks[i];
        ngx_log_error(NGX_LOG_WARN, cycle->log, 0, "initializing block %s", block->name.data);
        ngx_str_t         identity_file_full_path;

        identity_file_full_path = block->identity_file;

        if (ngx_conf_full_name(cycle, &identity_file_full_path, 0) != NGX_OK) {
            return NGX_ERROR;
        }

        block->ztx = Ziti_load_context((char*)block->identity_file.data);

        if(block->ztx == NULL){
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0, "for block %s could not load identity file %s (resolved to %s)", block->name.data, block->identity_file.data, identity_file_full_path.data);
        }

        ngx_str_t* services = block->services->elts;
        ngx_str_t* upstreams = block->upstreams->elts;
        for(ngx_uint_t y = 0; y < block->services->nelts; y++){
            ngx_str_t service = services[y];

            ngx_ziti_service_ctx_t ziti_service_ctx = {
                    .block = block,
                    .service = services[y],
                    .upstream = upstreams[y]
            };

            ngx_log_error(NGX_LOG_WARN, cycle->log, 0, "offloading service start for block %s service %s", block->name.data, service.data);

            ngx_int_t offload_result = ngx_ziti_run_service_offload(cycle, ziti_service_ctx);

            if(offload_result != NGX_OK){
                return NGX_ERROR;
            }
        }
    }

    return NGX_OK;
}

ngx_module_t ngx_ziti_module = {
        NGX_MODULE_V1,
        &ngx_ziti_module_ctx, /* module context */
        ngx_ziti_commands, /* module directives */
        NGX_CORE_MODULE, /* module type */
        NULL, /* init master */
        NULL, /* init module */
        ngx_ziti_init_process, /* init process */
        NULL, /* init thread */
        NULL, /* exit thread */
        NULL, /* exit process */
        NULL, /* exit master */
        NGX_MODULE_V1_PADDING
};

/**
 * Configuration setup function that installs the content handler.
 *
 * @param cf
 *   Module configuration structure pointer.
 * @param cmd
 *   Module directives structure pointer.
 * @param conf
 *   Module configuration structure pointer.
 * @return string
 *   Status of the configuration setup.
 */
static char *ngx_ziti(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_ziti_conf_t* ziti_conf = conf;
    ngx_str_t *values = cf->args->elts;
    ngx_str_t block_name = values[1];
    ngx_thread_pool_t          *tp;

#if (NGX_THREADS)
    //multiple adds are fine, they are cached by name
    tp = ngx_thread_pool_add(cf, &ngx_ziti_thread_pool_name);


    if (tp == NULL) {
        return NGX_CONF_ERROR;
    }
#else
    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "\"aio threads\" "
                           "is unsupported on this platform");
        return NGX_ERROR;
#endif

    if(block_name.data == NULL || block_name.len == 0){
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "ziti block name must be set");
        return NGX_CONF_ERROR;
    }

    ziti_conf->cur_block = ngx_ziti_new_block(cf, block_name);

    ngx_uint_t orig_cmd_type = cf->cmd_type;

    cf->cmd_type = NGX_ZITI_CONF;

    char *rv = ngx_conf_parse(cf, NULL);

    if (rv != NGX_CONF_OK) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "could not parse ziti block %s", block_name.data);
        return NGX_CONF_ERROR;
    }

    cf->cmd_type = orig_cmd_type;

    if(rv == NGX_CONF_OK){
        ngx_ziti_block_conf_t** pt_block = ngx_array_push(ziti_conf->blocks);

        if(pt_block == NULL) {
            return NGX_CONF_ERROR;
        }

        *pt_block = ziti_conf->cur_block;
        ziti_conf->cur_block = NULL;
    }

    return rv;
} /* ngx_ziti */

static char* ngx_ziti_identity_file(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_ziti_conf_t* ziti_conf = conf;
    ngx_str_t *values = cf->args->elts;
    ngx_str_t identity_file = values[1];

    if(identity_file.data == NULL || identity_file.len == 0){
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "ziti block %s must have an identity_file set", ziti_conf->cur_block->name.data);
        return NGX_CONF_ERROR;
    }

    ziti_conf->cur_block->identity_file = identity_file;

    return NGX_CONF_OK;
}

static char* ngx_ziti_bind(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_ziti_conf_t* ziti_conf = conf;
    ngx_str_t *values = cf->args->elts;
    ngx_str_t service_name = values[1];

    if(service_name.data == NULL || service_name.len == 0){
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "ziti block %s must have a service name set for bind", ziti_conf->cur_block->name.data);
        return NGX_CONF_ERROR;
    }

    ziti_conf->cur_block->num_services++;
    ngx_str_t* pt_push = ngx_array_push(ziti_conf->cur_block->services);

    if(pt_push == NULL){
        return NGX_CONF_ERROR;
    }

    *pt_push = service_name;

    ngx_uint_t orig_cmd_type = cf->cmd_type;

    cf->cmd_type = NGX_ZITI_BIND_CONF;

    char *rv = ngx_conf_parse(cf, NULL);

    cf->cmd_type = orig_cmd_type;

    if (rv != NGX_CONF_OK) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "could not parse ziti bind block with service name %s", service_name.data);
        return NGX_CONF_ERROR;
    }

    return rv;
}

static char* ngx_ziti_bind_upstream(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_ziti_conf_t* ziti_conf = conf;
    ngx_str_t *values = cf->args->elts;
    ngx_str_t upstream = values[1];

    if(upstream.data == NULL || upstream.len == 0){
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "ziti block %s must have a upstream set", ziti_conf->cur_block->name.data);
        return NGX_CONF_ERROR;
    }

    ngx_str_t* pt_push = ngx_array_push(ziti_conf->cur_block->upstreams);
    *pt_push = upstream;
    return NGX_CONF_OK;
}


