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
sudo apt install libuv1-dev

wget 'http://nginx.org/download/nginx-1.23.2.tar.gz'
tar -xzvf nginx-1.23.2.tar.gz

# See https://github.com/openziti/ziti-sdk-c/releases for other platforms
wget 'https://github.com/openziti/ziti-sdk-c/releases/download/0.30.8/ziti-sdk-0.30.8-Linux-x86_64.zip'
unzip ziti-sdk-0.30.8-Linux-x86_64.zip -d ziti-sdk-0.30.8-Linux-x86_64 

wget "https://github.com/openziti/uv-mbed/archive/refs/tags/v0.14.11.tar.gz"
mv v0.14.11.tar.gz uv_mbed-0.14.11.tar.gz
tar -xzvf uv_mbed-0.14.11.tar.gz

cd nginx-1.23.2/

# Here we assume you would install you nginx under /opt/nginx/.
cd nginx-1.23.2/; ./configure         /
      --prefix=/opt/nginx             /
      --add-module=../                / 
      --with-threads                  / 
      --with-ld-opt=../ziti-sdk-0.30.8-Linux-x86_64/lib/libziti.so / 
      --with-cc-opt="-I ../include -I ../uv-mbed-0.14.11/include"
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
    identity_file /path/to/ziti/identity.json;

    bind http-service {
        upstream localhost:7070;
    }
}
```
