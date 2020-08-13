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

#include <dsn/http/http_server.h>
#include <dsn/tool_api.h>
#include <boost/algorithm/string.hpp>
#include <fmt/ostream.h>
#include <dsn/dist/fmt_logging.h>
#include <dsn/utility/errors.h>
#include <boost/utility/string_view.hpp>
#include <nlohmann/json.hpp>

#include "http_message_parser.h"
#include "uri_decoder.h"
#include "http_server_impl.h"

namespace dsn {

/*extern*/ std::string http_status_code_to_string(http_status_code code)
{
    switch (code) {
    case http_status_code::ok:
        return "200 OK";
    case http_status_code::temporary_redirect:
        return "307 Temporary Redirect";
    case http_status_code::bad_request:
        return "400 Bad Request";
    case http_status_code::not_found:
        return "404 Not Found";
    case http_status_code::internal_server_error:
        return "500 Internal Server Error";
    default:
        dfatal("invalid code: %d", code);
        __builtin_unreachable();
    }
}

/*extern*/ http_call_builder register_http_call(std::string full_path)
{
    http_call_builder builder(std::move(full_path));
    // register this call
    http_call_registry::instance().add(std::move(builder._call));
    return builder;
}

/*extern*/ void deregister_http_call(const std::string &full_path)
{
    http_call_registry::instance().remove(full_path);
}

int64_t http_argument::get_int() const
{
    dcheck_eq(type, HTTP_ARG_INT);
    int64_t val = 0;
    dcheck_eq(buf2int64(_value, val), true);
    return val;
}

bool http_argument::get_bool() const
{
    dcheck_eq(type, HTTP_ARG_BOOLEAN);
    bool val = false;
    dcheck_eq(buf2bool(_value, val), true);
    return val;
}

std::string http_argument::get_string() const
{
    dcheck_eq(type, HTTP_ARG_STRING);
    return _value;
}

bool http_argument::set_value(std::string value)
{
    _value = std::move(value);
    if (type == HTTP_ARG_STRING) {
        return true;
    }
    if (type == HTTP_ARG_INT) {
        int64_t val;
        return buf2int64(_value, val);
    }
    if (type == HTTP_ARG_BOOLEAN) {
        bool val;
        return buf2bool(_value, val);
    }
    return false;
}

http_service::http_service(std::string root_path) : _root_path(std::move(root_path)) {}

http_service::~http_service()
{
    for (const std::string &p : _path_list) {
        deregister_http_call(p);
    }
}

http_call_builder http_service::register_handler(std::string path)
{
    path = _root_path + '/' + path;
    _path_list.push_back(path);
    return register_http_call(std::move(path));
}

http_server::http_server(bool start /*default=true*/) : serverlet<http_server>("http_server")
{
    if (!start) {
        return;
    }

    register_rpc_handler(RPC_HTTP_SERVICE, "http_service", &http_server::serve);

    tools::register_message_header_parser<http_message_parser>(NET_HDR_HTTP, {"GET ", "POST"});
}

void http_server::serve(message_ex *msg)
{
    error_with<http_request> res = parse_http_request(msg);
    http_response resp;
    if (!res.is_ok()) {
        resp.status_code = http_status_code::bad_request;
        resp.body = fmt::format("failed to parse request: {}", res.get_error());
    } else {
        const http_request &req = res.get_value();
        auto call = http_call_registry::instance().find(req.path);
        if (call != nullptr) {
            call->callback(req, resp);
        } else {
            resp.status_code = http_status_code::not_found;
            resp.body = fmt::format("service not found for \"{}\"", req.path);
        }
    }
    http_response_reply(resp, msg);
}

static error_s set_argument_if_ok(std::string arg_key,
                                  std::string arg_val,
                                  const http_call &call,
                                  http_request &req)
{
    auto it = call.args_map.find(arg_key);
    if (it == call.args_map.end()) {
        return FMT_ERR(ERR_INVALID_PARAMETERS, "\"{}\"=\"{}\"", arg_key, arg_val);
    }
    auto arg = std::make_shared<http_argument>(std::move(arg_key), it->second);
    if (!arg->set_value(std::move(arg_val))) {
        return FMT_ERR(ERR_INVALID_PARAMETERS, "\"{}\"=\"{}\"", arg_key, arg_val);
    }
    req.query_args[arg->name] = std::move(arg);
    return error_s::ok();
}

static error_s
parse_url_query_string(std::string query_string, const http_call &call, http_request &req)
{
    if (query_string.empty()) {
        return error_s::ok();
    }
    // decode resolved query
    auto decoded_unresolved_query = uri::decode(query_string);
    if (!decoded_unresolved_query.is_ok()) {
        return decoded_unresolved_query.get_error();
    }
    query_string = decoded_unresolved_query.get_value();

    // find if there are search-params (?<arg>=<val>&<arg>=<val>)
    std::vector<std::string> search_params;
    boost::split(search_params, query_string, boost::is_any_of("&"));
    for (const std::string &arg_val : search_params) {
        size_t sep = arg_val.find_first_of('=');
        std::string name(arg_val.substr(0, sep));
        std::string value;
        if (sep != std::string::npos) {
            value = std::string(arg_val.substr(sep + 1));
        }
        RETURN_NOT_OK(set_argument_if_ok(std::move(name), std::move(value), call, req));
    }
    return error_s::ok();
}

static error_s parse_http_request_body(const std::string &content_type,
                                       std::string body,
                                       const http_call &call,
                                       http_request &req)
{
    if (content_type.find("application/json") != std::string::npos) {
        nlohmann::json json = nlohmann::json::parse(body, nullptr, false);
        if (json.is_discarded()) {
            return error_s::make(ERR_INVALID_PARAMETERS, "failed to parse json");
        }
        for (const auto &el : json.items()) {
            RETURN_NOT_OK(set_argument_if_ok(el.key(), el.value(), call, req));
        }
        return error_s::ok();
    }
    if (content_type.find("application/x-www-form-urlencoded") != std::string::npos) {
        return parse_url_query_string(body, call, req);
    }
    if (content_type.find("text/plain") != std::string::npos) {
        req.body = std::move(body);
        return error_s::ok();
    }
    return FMT_ERR(ERR_INVALID_PARAMETERS, "unsupported Content-Type \"{}\"", content_type);
}

/*extern*/ error_with<http_request> parse_http_request(message_ex *m)
{
    if (m->buffers.size() != 3) {
        return error_s::make(ERR_INVALID_DATA,
                             std::string("buffer size is: ") + std::to_string(m->buffers.size()));
    }

    http_request ret;
    std::string body = m->buffers[1].to_string();
    blob full_url = m->buffers[2];
    ret.method = static_cast<http_method>(m->header->hdr_type);

    http_parser_url u{0};
    http_parser_parse_url(full_url.data(), full_url.length(), false, &u);

    ret.path.clear();
    if (u.field_set & (1u << UF_PATH)) {
        uint16_t data_length = u.field_data[UF_PATH].len;
        ret.path.resize(data_length);
        strncpy(&ret.path[0], full_url.data() + u.field_data[UF_PATH].off, data_length);
    }

    std::shared_ptr<http_call> call = http_call_registry::instance().find(ret.path);
    if (call == nullptr) {
        return FMT_ERR(ERR_INVALID_PARAMETERS, "no resource under path \"{}\"", ret.path);
    }

    std::string query_string;
    if (u.field_set & (1u << UF_QUERY)) {
        uint16_t data_length = u.field_data[UF_QUERY].len;
        query_string.resize(data_length);
        strncpy(&query_string[0], full_url.data() + u.field_data[UF_QUERY].off, data_length);

        RETURN_NOT_OK(parse_url_query_string(std::move(query_string), *call, ret));
    }

    std::string content_type = m->buffers[3].to_string();
    RETURN_NOT_OK(parse_http_request_body(content_type, body, *call, ret));
    return ret;
}

/*extern*/ void http_response_reply(const http_response &resp, message_ex *req)
{
    message_ptr resp_msg = req->create_response();

    std::ostringstream os;
    os << "HTTP/1.1 " << http_status_code_to_string(resp.status_code) << "\r\n";
    os << "Content-Type: " << resp.content_type << "\r\n";
    os << "Content-Length: " << resp.body.length() << "\r\n";
    if (!resp.location.empty()) {
        os << "Location: " << resp.location << "\r\n";
    }
    os << "\r\n";
    os << resp.body;

    rpc_write_stream writer(resp_msg.get());
    writer.write(os.str().data(), os.str().length());
    writer.flush();

    dsn_rpc_reply(resp_msg.get());
}

/*extern*/ void start_http_server() { static http_server server; }

} // namespace dsn
