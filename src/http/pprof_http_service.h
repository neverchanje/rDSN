// Copyright (c) 2019, Xiaomi, Inc.  All rights reserved.
// This source code is licensed under the Apache License Version 2.0, which
// can be found in the LICENSE file in the root directory of this source tree.

#pragma once

#ifdef DSN_ENABLE_GPERF

#include <dsn/http/http_server.h>

namespace dsn {

class pprof_http_service : public http_service
{
public:
    pprof_http_service() : http_service("pprof")
    {
        register_handler("heap")
            .callback(std::bind(&pprof_http_service::heap_handler,
                                this,
                                std::placeholders::_1,
                                std::placeholders::_2))
            .add_argument("seconds", HTTP_ARG_INT);
        register_handler("symbol").callback(std::bind(&pprof_http_service::symbol_handler,
                                                      this,
                                                      std::placeholders::_1,
                                                      std::placeholders::_2));
        register_handler("cmdline").callback(std::bind(&pprof_http_service::cmdline_handler,
                                                       this,
                                                       std::placeholders::_1,
                                                       std::placeholders::_2));
        register_handler("growth").callback(std::bind(&pprof_http_service::growth_handler,
                                                      this,
                                                      std::placeholders::_1,
                                                      std::placeholders::_2));
        register_handler("profile")
            .callback(std::bind(&pprof_http_service::profile_handler,
                                this,
                                std::placeholders::_1,
                                std::placeholders::_2))
            .add_argument("seconds", HTTP_ARG_INT);
    }

    void heap_handler(const http_request &req, http_response &resp);

    void symbol_handler(const http_request &req, http_response &resp);

    void cmdline_handler(const http_request &req, http_response &resp);

    void growth_handler(const http_request &req, http_response &resp);

    void profile_handler(const http_request &req, http_response &resp);

private:
    std::atomic_bool _in_pprof_action{false};
};

} // namespace dsn

#endif // DSN_ENABLE_GPERF
