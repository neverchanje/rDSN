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

#pragma once

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <unordered_map>

namespace dsn {

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

private:
    std::string _value;
};

struct http_request
{
    std::unordered_map<std::string, std::shared_ptr<http_argument>> query_args;
    std::string body;
    std::string path;
    http_method method;
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
};

// Helper to construct a http_call.
class http_call_builder
{
public:
    http_call_builder(std::string path) : _call(std::make_shared<http_call>())
    {
        _call->path = std::move(path);
    }
    http_call_builder &help(std::string help)
    {
        _call->help = std::move(help);
        return *this;
    }
    http_call_builder &add_argument(std::string name, http_argument_type type)
    {
        _call->args_map.emplace(std::move(name), type);
        return *this;
    }
    http_call_builder &callback(http_callback callback)
    {
        _call->callback = std::move(callback);
        return *this;
    }

private:
    friend http_call_builder register_http_call(std::string full_path);
    std::shared_ptr<http_call> _call;
};

// Example:
//
// ```
// register_http_call("/meta/app")
//     .callback(std::bind(&meta_http_service::get_app_handler,
//                         this,
//                         std::placeholders::_1,
//                         std::placeholders::_2))
//     .add_argument("app_name", HTTP_ARG_STRING);
// ```
extern http_call_builder register_http_call(std::string full_path);

// Deregister the HTTP call.
extern void deregister_http_call(std::string full_path);

// A suite of HTTP handlers coupled using the same prefix of the service.
// If a handler is registered with path 'app/duplication', its real path is
// "/<root_path>/app/duplication".
class http_service
{
public:
    explicit http_service(std::string root_path);

    virtual ~http_service();

    http_call_builder register_handler(std::string path);

private:
    std::string _root_path;
    std::vector<std::string> _path_list;
};

// Starts serving HTTP requests.
// The internal HTTP server will reuse the rDSN server port.
extern void start_http_server();

// Stops serving HTTP requests.
extern void stop_http_server();

} // namespace dsn
