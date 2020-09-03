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

#include "replica.h"

namespace dsn {
namespace replication {

/// What to be done before/after the process that a write handled by the primary replica.
class two_pc_intercept
{
public:
    /// Intercepts before where a 2PC begins.
    /// If an error occurs, the 2PC will be cancelled and reply to client with the error.
    static error_code before(replica *r);

    /// Intercepts after a 2PC completes.
    static error_code after(replica *r);

private:
    friend class replica;
    friend class replica_stub;
};

} // namespace replication
} // namespace dsn
