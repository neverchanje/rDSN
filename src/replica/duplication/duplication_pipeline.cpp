// Copyright (c) 2017-present, Xiaomi, Inc.  All rights reserved.
// This source code is licensed under the Apache License Version 2.0, which
// can be found in the LICENSE file in the root directory of this source tree.

#include <dsn/dist/replication/replication_app_base.h>
#include <dsn/dist/fmt_logging.h>
#include <dsn/utility/flags.h>

#include "replica/replica_stub.h"
#include "duplication_pipeline.h"
#include "load_from_private_log.h"

namespace dsn {
namespace replication {

DSN_DEFINE_uint64(
    "replication",
    dup_local_lagging_write_threshold,
    10000,
    "The duplicated write with latency exceeds the threshold will be logged."
    "The latency is the duration that this write stayed locally, from when it's generated,"
    "to when its shipping begins.");

//                     //
// mutation_duplicator //
//                     //

/*static*/ std::function<std::unique_ptr<mutation_duplicator>(
    replica_base *, string_view /*remote cluster*/, string_view /*app*/)>
    mutation_duplicator::creator;

//               //
// load_mutation //
//               //

void load_mutation::run()
{
    decree last_decree = _duplicator->progress().last_decree;
    _start_decree = last_decree + 1;
    if (_replica->private_log()->max_commit_on_disk() < _start_decree) {
        // wait 100ms for next try if no mutation was added.
        repeat(100_ms);
        return;
    }

    _log_on_disk->set_start_decree(_start_decree);
    _log_on_disk->async();
}

load_mutation::~load_mutation() = default;

load_mutation::load_mutation(replica_duplicator *duplicator,
                             replica *r,
                             load_from_private_log *load_private)
    : replica_base(r), _log_on_disk(load_private), _replica(r), _duplicator(duplicator)
{
}

//               //
// ship_mutation //
//               //

void ship_mutation::ship(mutation_tuple_set &&in)
{
    for (const mutation_tuple &tu : in) {
        uint64_t timestamp = std::get<0>(tu);
        uint64_t time_lag = (dsn_now_us() - timestamp) * 1000 /*us to ns*/;
        if (time_lag > FLAGS_dup_local_lagging_write_threshold * 1000 * 1000) {
            dwarn_replica("slow duplication of write: {}ms", time_lag / 1000 / 1000);
        }
        _counter_dup_local_time_lag->set(time_lag);
    }
    _mutation_duplicator->duplicate(std::move(in), [this](size_t total_shipped_size) mutable {
        update_progress();
        _counter_dup_shipped_bytes_rate->add(total_shipped_size);
        step_down_next_stage();
    });
}

void ship_mutation::run(decree &&last_decree, mutation_tuple_set &&in)
{
    _last_decree = last_decree;

    if (in.empty()) {
        update_progress();
        step_down_next_stage();
        return;
    }

    ship(std::move(in));
}

void ship_mutation::update_progress()
{
    dcheck_eq_replica(
        _duplicator->update_progress(duplication_progress().set_last_decree(_last_decree)),
        error_s::ok());

    // committed decree never decreases
    decree last_committed_decree = _replica->last_committed_decree();
    dcheck_ge_replica(last_committed_decree, _last_decree);
}

ship_mutation::ship_mutation(replica_duplicator *duplicator)
    : replica_base(duplicator),
      _duplicator(duplicator),
      _replica(duplicator->_replica),
      _stub(duplicator->_replica->get_replica_stub())
{
    _mutation_duplicator = new_mutation_duplicator(
        duplicator, _duplicator->remote_cluster_name(), _replica->get_app_info()->app_name);
    _mutation_duplicator->set_task_environment(duplicator);

    _counter_dup_shipped_bytes_rate.init_app_counter("eon.replica_stub",
                                                     "dup.shipped_bytes_rate",
                                                     COUNTER_TYPE_RATE,
                                                     "shipping rate of private log in bytes");

    _counter_dup_local_time_lag.init_app_counter(
        "eon.replica_stub",
        "dup.local_time_tag",
        COUNTER_TYPE_NUMBER_PERCENTILES,
        "latency of a mutation from being handled to its shipping begins");
}

} // namespace replication
} // namespace dsn
