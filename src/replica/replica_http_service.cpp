// Copyright (c) 2017-present, Xiaomi, Inc.  All rights reserved.
// This source code is licensed under the Apache License Version 2.0, which
// can be found in the LICENSE file in the root directory of this source tree.

#include <nlohmann/json.hpp>
#include <fmt/format.h>
#include "replica_http_service.h"
#include "duplication/duplication_sync_timer.h"

namespace dsn {
namespace replication {

void replica_http_service::query_duplication_handler(const http_request &req, http_response &resp)
{
    if (!_stub->_duplication_sync_timer) {
        resp.body = "duplication is not enabled [duplication_enabled=false]";
        resp.status_code = http_status_code::not_found;
        return;
    }
    auto appid = static_cast<int32_t>(req.get_arg_int("appid"));
    bool app_found = false;
    auto states = _stub->_duplication_sync_timer->get_dup_states(appid, &app_found);
    if (!app_found) {
        resp.status_code = http_status_code::not_found;
        resp.body = fmt::format("no primary for app [appid={}]", appid);
        return;
    }
    if (states.empty()) {
        resp.status_code = http_status_code::not_found;
        resp.body = fmt::format("no duplication assigned for app [appid={}]", appid);
        return;
    }

    nlohmann::json json;
    for (const auto &s : states) {
        json[std::to_string(s.first)][s.second.id.to_string()] = nlohmann::json{
            {"duplicating", s.second.duplicating},
            {"not_confirmed_mutations_num", s.second.not_confirmed},
            {"not_duplicated_mutations_num", s.second.not_duplicated},
            {"fail_mode", duplication_fail_mode_to_string(s.second.fail_mode)},
        };
    }
    resp.status_code = http_status_code::ok;
    resp.body = json.dump();
}

} // namespace replication
} // namespace dsn
