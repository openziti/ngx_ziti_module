set(NGINX_CONFIGURE_ARGS "--with-threads" "--with-compat")

if (DEBUG)
    set(NGINX_CONFIGURE_ARGS "--with-threads" "--with-compat" "--with-debug")
endif()

message("args ${NGINX_CONFIGURE_ARGS}")

# build against nginx at the version specified by GIT_TAG. nginx uses custom auto configuration scripts that
# creates header files that are specific to the current host and output to <nginx dir>/objs/*.h.
ExternalProject_Add(
        nginx
        PREFIX ${CMAKE_BINARY_DIR}/_deps/nginx
        GIT_REPOSITORY https://github.com/nginx/nginx.git
        GIT_TAG release-1.23.2
        TIMEOUT 10
        CONFIGURE_COMMAND ./auto/configure ${NGINX_CONFIGURE_ARGS}
        INSTALL_COMMAND "" #empty install command to disable install
        UPDATE_COMMAND "" #empty update command to disable update
        LOG_DOWNLOAD ON
        BUILD_IN_SOURCE 1 #build inside of the downloaded repo's source directory as expected by auto/configure
)

if(NOT DEFINED $ENV{ZITI_SDK_C_BRANCH})
    SET(ZITI_SDK_C_BRANCH "main")
endif()

FetchContent_Declare(ziti-sdk-c
        GIT_REPOSITORY https://github.com/openziti/ziti-sdk-c.git
        GIT_TAG ${ZITI_SDK_C_BRANCH}
        )


set(ZITI_BUILD_TESTS off)
set(ZITI_BUILD_PROGRAMS off)
FetchContent_MakeAvailable(ziti-sdk-c)

add_dependencies(${PROJECT_NAME} nginx)

ExternalProject_Get_property(nginx SOURCE_DIR)
message("project name ${PROJECT_NAME}")
target_include_directories(${PROJECT_NAME}
        PUBLIC "${SOURCE_DIR}/objs"
        PUBLIC "${SOURCE_DIR}/src/core"
        PUBLIC "${SOURCE_DIR}/src/event"
        PUBLIC "${SOURCE_DIR}/src/event/modules"
        PUBLIC "${SOURCE_DIR}/src/http"
        PUBLIC "${SOURCE_DIR}/src/http/modules"
        PUBLIC "${SOURCE_DIR}/src/mail"
        PUBLIC "${SOURCE_DIR}/src/stream"
        PUBLIC "${SOURCE_DIR}/src/os/unix"
        )

target_link_libraries(${PROJECT_NAME} ziti)