// Copyright (c) 2018, Xiaomi, Inc.  All rights reserved.
// This source code is licensed under the Apache License Version 2.0, which
// can be found in the LICENSE file in the root directory of this source tree.

#include <dsn/http/http_server.h>
#include <dsn/tool_api.h>
#include <dsn/utility/time_utils.h>
#include <dsn/utility/flags.h>
#include <dsn/dist/fmt_logging.h>
#include <boost/algorithm/string.hpp>
#include <fmt/ostream.h>

#include "http_message_parser.h"
#include "pprof_http_service.h"
#include "builtin_http_calls.h"
#include "uri_decoder.h"
#include "http_call_registry.h"
#include "http_server_impl.h"

namespace dsn {

DSN_DEFINE_bool("http", enable_http_server, true, "whether to enable the embedded HTTP server");

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

/*extern*/ http_call &register_http_call(std::string full_path)
{
    auto call_ptr = dsn::make_unique<http_call>();
    call_ptr->path = std::move(full_path);
    http_call &call = *call_ptr;
    http_call_registry::instance().add(std::move(call_ptr));
    return call;
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

void http_service::register_handler(std::string path, http_callback cb, std::string help)
{
    if (!FLAGS_enable_http_server) {
        return;
    }
    auto call = make_unique<http_call>();
    call->path = this->path();
    if (!path.empty()) {
        call->path += "/" + std::move(path);
    }
    call->callback = std::move(cb);
    call->help = std::move(help);
    http_call_registry::instance().add(std::move(call));
}

http_server::http_server() : serverlet<http_server>("http_server")
{
    if (!FLAGS_enable_http_server) {
        return;
    }

    register_rpc_handler(RPC_HTTP_SERVICE, "http_service", &http_server::serve);

    tools::register_message_header_parser<http_message_parser>(NET_HDR_HTTP, {"GET ", "POST"});

    // add builtin services
    register_builtin_http_calls();
}

void http_server::serve(message_ex *msg)
{
    error_with<http_request> res = http_request::parse(msg);
    http_response resp;
    if (!res.is_ok()) {
        resp.status_code = http_status_code::bad_request;
        resp.body = fmt::format("failed to parse request: {}", res.get_error());
    } else {
        const http_request &req = res.get_value();
        std::shared_ptr<http_call> call = http_call_registry::instance().find(req.path);
        if (call != nullptr) {
            call->callback(req, resp);
        } else {
            resp.status_code = http_status_code::not_found;
            resp.body = fmt::format("service not found for \"{}\"", req.path);
        }
    }

    http_response_reply(resp, msg);
}

/*extern*/ error_s set_argument_if_ok(std::string arg_key, std::string arg_val, http_request &req)
{
    const http_call &call = *req.call;
    auto it = call.args_map.find(arg_key);
    if (it == call.args_map.end()) {
        return FMT_ERR(ERR_INVALID_PARAMETERS, "invalid name \"{}\"", arg_key);
    }
    auto arg = std::make_shared<http_argument>(std::move(arg_key), it->second);
    if (!arg->set_value(std::move(arg_val))) {
        return FMT_ERR(ERR_INVALID_PARAMETERS, "invalid value \"{}\"", arg_val);
    }
    req.query_args[arg->name] = std::move(arg);
    return error_s::ok();
}

static error_s parse_url_query_string(std::string query_string, http_request &req)
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
        RETURN_NOT_OK(set_argument_if_ok(std::move(name), std::move(value), req));
    }
    return error_s::ok();
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
    ret.call = std::move(call);

    std::string query_string;
    if (u.field_set & (1u << UF_QUERY)) {
        uint16_t data_length = u.field_data[UF_QUERY].len;
        query_string.resize(data_length);
        strncpy(&query_string[0], full_url.data() + u.field_data[UF_QUERY].off, data_length);

        RETURN_NOT_OK(parse_url_query_string(std::move(query_string), ret));
    }

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

/*extern*/ void start_http_server()
{
    // starts http server as a singleton
    static http_server server;
}

/*extern*/ void register_http_service(http_service *svc)
{
    // simply hosting the memory of these http services.
    static std::vector<std::unique_ptr<http_service>> services_holder;
    static std::mutex mu;

    std::lock_guard<std::mutex> guard(mu);
    services_holder.push_back(std::unique_ptr<http_service>(svc));
}

} // namespace dsn
