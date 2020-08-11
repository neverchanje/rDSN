// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "http_server_impl.h"
#include "pprof_http_service.h"

#include <dsn/utility/output_utils.h>
#include <dsn/utility/time_utils.h>

namespace dsn {

/*extern*/ void register_builtin_http_calls()
{
#ifdef DSN_ENABLE_GPERF
    static pprof_http_service pprof_svc;
#endif

    register_http_call("").callback([](const http_request &req, http_response &resp) {
        resp.body = get_all_help_info();
        resp.status_code = http_status_code::ok;
    });

    register_http_call("recentStartTime")
        .callback([](const http_request &req, http_response &resp) {
            char start_time[100];
            dsn::utils::time_ms_to_date_time(dsn::utils::process_start_millis(), start_time, 100);
            std::ostringstream out;
            dsn::utils::table_printer tp;
            tp.add_row_name_and_data("RecentStartTime", start_time);
            tp.output(out, dsn::utils::table_printer::output_format::kJsonCompact);

            resp.body = out.str();
            resp.status_code = http_status_code::ok;
        });

    register_http_call("perfCounter")
        .callback(get_perf_counter_handler)
        .add_argument("name", HTTP_ARG_STRING);
}

} // namespace dsn
