// Copyright (c) 2017-present, Xiaomi, Inc.  All rights reserved.
// This source code is licensed under the Apache License Version 2.0, which
// can be found in the LICENSE file in the root directory of this source tree.

#pragma once

#include <algorithm>

#include <dsn/http/http_server.h>

namespace dsn {
namespace replication {

class meta_service;
class meta_http_service final : http_service
{
public:
    explicit meta_http_service(meta_service *s) : http_service("meta"), _service(s)
    {
        register_handler("app")
            .callback(std::bind(&meta_http_service::get_app_handler,
                                this,
                                std::placeholders::_1,
                                std::placeholders::_2))
            .add_argument("name", HTTP_ARG_STRING)
            .add_argument("detail", HTTP_ARG_BOOLEAN);

        register_handler("app/duplication")
            .callback(std::bind(&meta_http_service::query_duplication_handler,
                                this,
                                std::placeholders::_1,
                                std::placeholders::_2))
            .add_argument("name", HTTP_ARG_STRING);

        register_handler("apps")
            .callback(std::bind(&meta_http_service::list_app_handler,
                                this,
                                std::placeholders::_1,
                                std::placeholders::_2))
            .add_argument("detail", HTTP_ARG_BOOLEAN);

        register_handler("nodes")
            .callback(std::bind(&meta_http_service::list_node_handler,
                                this,
                                std::placeholders::_1,
                                std::placeholders::_2))
            .add_argument("detail", HTTP_ARG_BOOLEAN);

        register_handler("cluster").callback(std::bind(&meta_http_service::get_cluster_info_handler,
                                                       this,
                                                       std::placeholders::_1,
                                                       std::placeholders::_2));

        register_handler("app_envs")
            .callback(std::bind(&meta_http_service::get_app_envs_handler,
                                this,
                                std::placeholders::_1,
                                std::placeholders::_2))
            .add_argument("name", HTTP_ARG_STRING);

        register_handler("backup_policy")
            .callback(std::bind(&meta_http_service::query_backup_policy_handler,
                                this,
                                std::placeholders::_1,
                                std::placeholders::_2))
            .add_argument("name", HTTP_ARG_STRING);
    }

    void get_app_handler(const http_request &req, http_response &resp);
    void list_app_handler(const http_request &req, http_response &resp);
    void list_node_handler(const http_request &req, http_response &resp);
    void get_cluster_info_handler(const http_request &req, http_response &resp);
    void get_app_envs_handler(const http_request &req, http_response &resp);
    void query_backup_policy_handler(const http_request &req, http_response &resp);
    void query_duplication_handler(const http_request &req, http_response &resp);

private:
    // set redirect location if current server is not primary
    bool redirect_if_not_primary(const http_request &req, http_response &resp);

    meta_service *_service;
};

} // namespace replication
} // namespace dsn
