// Copyright (c) 2018, Xiaomi, Inc.  All rights reserved.
// This source code is licensed under the Apache License Version 2.0, which
// can be found in the LICENSE file in the root directory of this source tree.

#pragma once

#include <dsn/utility/flags.h>
#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <unordered_map>

namespace dsn {

DSN_DECLARE_bool(enable_http_server);

// The allowed HTTP methods. Otherwise the server will not
// respond to the request.
enum http_method
{
    HTTP_METHOD_GET = 1,
    HTTP_METHOD_POST = 2,
};

// The argument types for an HTTP request.
// If any type check failed, 400 status code is returned.
enum http_argument_type
{
    HTTP_ARG_INT,
    HTTP_ARG_STRING,
    HTTP_ARG_BOOLEAN,
};

// An argument could be in percent-encoded params, which starts with '?' (<url>?name=value), [1]
// or in the HTML form with "Content-Type: application/x-www-form-urlencoded" [2]
// and "application/json" [3].
//
// [1] https://developer.mozilla.org/en-US/docs/Web/API/URLSearchParams/URLSearchParams
// [2] https://developer.mozilla.org/en-US/docs/Web/HTTP/Methods/POST
// [3] https://www.w3.org/TR/html-json-forms/
struct http_argument
{
    const std::string name;
    const http_argument_type type;

    http_argument(std::string nm, http_argument_type tp) : name(std::move(nm)), type(tp) {}

    int64_t get_int() const;
    bool get_bool() const;
    std::string get_string() const;

    // Returns true for success.
    bool set_value(std::string value);

    // Returns not-parsed argument value.
    const std::string &get_raw_value() const { return _value; }

private:
    std::string _value;
};

struct http_call;
struct http_request
{
    std::unordered_map<std::string, std::shared_ptr<http_argument>> query_args;
    std::string body;
    std::string path;
    http_method method;
    std::shared_ptr<http_call> call;

    int64_t get_arg_int(const std::string &arg) const
    {
        return query_args.find(arg)->second->get_int();
    }
    std::string get_arg_string(const std::string &arg) const
    {
        return query_args.find(arg)->second->get_string();
    }
    bool get_arg_bool(const std::string &arg) const
    {
        auto it = query_args.find(arg);
        if (it == query_args.end()) {
            return false;
        }
        return it->second->get_bool();
    }
};

enum class http_status_code
{
    ok,                    // 200
    temporary_redirect,    // 307
    bad_request,           // 400
    not_found,             // 404
    internal_server_error, // 500
};

extern std::string http_status_code_to_string(http_status_code code);

struct http_response
{
    std::string body;
    http_status_code status_code{http_status_code::ok};
    std::string content_type = "text/plain";
    std::string location;
};

typedef std::function<void(const http_request &req, http_response &resp)> http_callback;

// Defines the structure of an HTTP call.
struct http_call
{
    std::string path;
    std::string help;
    http_callback callback;
    std::unordered_map<std::string, http_argument_type> args_map;

    http_call &with_callback(http_callback cb)
    {
        callback = std::move(cb);
        return *this;
    }
    http_call &with_help(std::string hp)
    {
        help = std::move(hp);
        return *this;
    }
    http_call &add_argument(std::string name, http_argument_type type)
    {
        args_map.emplace(std::move(name), type);
        return *this;
    }
};

// A suite of HTTP handlers coupled using the same prefix of the service.
// If a handler is registered with path 'app/duplication', its real path is
// "/<root_path>/app/duplication".
class http_service
{
public:
    virtual ~http_service() = default;

    virtual std::string path() const = 0;

    void register_handler(std::string path, http_callback cb, std::string help);
};

// Example:
//
// ```
// register_http_call("/meta/app")
//     .with_callback(std::bind(&meta_http_service::get_app_handler,
//                              this,
//                              std::placeholders::_1,
//                              std::placeholders::_2))
//     .with_help("Gets the app information")
//     .add_argument("app_name", HTTP_ARG_STRING);
// ```
extern http_call &register_http_call(std::string full_path);

// Starts serving HTTP requests.
// The internal HTTP server will reuse the rDSN server port.
extern void start_http_server();

// NOTE: the memory of `svc` will be transferred to the underlying registry.
// TODO(wutao): pass `svc` as a std::unique_ptr.
extern void register_http_service(http_service *svc);

} // namespace dsn
