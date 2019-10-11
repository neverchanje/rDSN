/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Microsoft Corporation
 *
 * -=- Robust Distributed System Nucleus (rDSN) -=-
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#pragma once

#include <atomic>

#include "dist/replication/lib/replica_stub.h"

#include <dsn/dist/replication/duplication_common.h>
#include <dsn/utility/chrono_literals.h>

namespace dsn {
namespace replication {

using namespace literals::chrono_literals;

constexpr int DUPLICATION_SYNC_PERIOD_SECOND = 10;

// Per-server(replica_stub)-instance.
class duplication_sync_timer
{
public:
    explicit duplication_sync_timer(replica_stub *stub);

    ~duplication_sync_timer();

    void start();

    void close();

private:
    // replica server periodically uploads current confirm points to meta server by sending
    // `duplication_sync_request`.
    // if success, meta server will respond with `duplication_sync_response`, which contains
    // the entire set of duplications on this server.
    void run();

    /// \param dup_map: <appid -> list<dup_entry>>
    void
    update_duplication_map(const std::map<app_id, std::map<dupid_t, duplication_entry>> &dup_map);

    void on_duplication_sync_reply(error_code err, const duplication_sync_response &resp);

    std::vector<replica_ptr> get_all_primaries();

    std::vector<replica_ptr> get_all_replicas();

    // == remote commands == //

    std::string enable_dup_sync(const std::vector<std::string> &args);

    std::string dup_state(const std::vector<std::string> &args);

private:
    friend class duplication_sync_timer_test;

    replica_stub *_stub{nullptr};

    task_ptr _timer_task;
    task_ptr _rpc_task;
    mutable zlock _lock; // protect _rpc_task

    dsn_handle_t _cmd_enable_dup_sync;
    dsn_handle_t _cmd_dup_state;
};

} // namespace replication
} // namespace dsn
