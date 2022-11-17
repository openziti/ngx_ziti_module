# ngx_ziti_module

This is an NGINX module that enables NGINX to `bind` OpenZiti servers and then forward incoming 
connections to upstream addresses. To compile this module and to run it, NGINX must be compiled 
with the `--with-threads` option to enable threading support. This is not available on Windows.
You can verify if `nginx` was compiled with `--with-threads` via `nginx -V`.

# Building

This module can be built in two fashions, it can be built via `nginx`'s build int auto configuration
scripts or via CMake. The CMake build uses nginx's auto configuration builds but allow for better
integration with moder IDE's like Clion. For CMake, point your toolchain at it. To run/debug/etc.
the following arguments are useful if the repository is your working directory`-c configs/nginx.conf -e logs/err.log -p ./`.


To use nginx's build system:

```shell
wget 'http://nginx.org/download/nginx-1.23.2.tar.gz'
tar -xzvf nginx-1.23.2.tar.gz
cd nginx-1.23.2/

# Here we assume you would install you nginx under /opt/nginx/.
./configure --prefix=/opt/nginx \
  --add-module=/path/to/ngx_ziti_module \
  --with-threads
make
make install
```


# Using

After building and installing the `ngx_ziti_module.so` the following configuration block
can be used to configure `nginx`:

```
load_module ngx_ziti_module.so;

error_log /dev/stderr debug;
error_log logs/error.log debug;

thread_pool ngx_ziti_tp threads=32 max_queue=65536;

events {
    worker_connections  1024;
}

ziti myZitiInstaceNameUsedForLogging {
    ziti_identity_file /path/to/ziti/identity.json;

    ziti_bind http-service {
        ziti_upstream localhost:7070;
    }
}
```
