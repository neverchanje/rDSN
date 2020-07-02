// Copyright (c) 2017-present, Xiaomi, Inc.  All rights reserved.
// This source code is licensed under the Apache License Version 2.0, which
// can be found in the LICENSE file in the root directory of this source tree.

#include "log_block.h"

#include <boost/interprocess/mapped_region.hpp>

namespace dsn {
namespace replication {

log_block::log_block(int64_t start_offset) : _start_offset(start_offset) { init(); }

log_block::log_block() { init(); }

void log_block::init()
{
    log_block_header hdr;

    binary_writer temp_writer;
    temp_writer.write_pod(hdr);
    add(temp_writer.get_buffer());
}

void log_appender::append_mutation(const mutation_ptr &mu, const aio_task_ptr &cb)
{
    _mutations.push_back(mu);
    if (cb) {
        _callbacks.push_back(cb);
    }
    log_block *blk = &_blocks.back();
    if (_blocks.back().size() > DEFAULT_MAX_BLOCK_BYTES) {
        blk = append_empty_block();
    }
    mu->data.header.log_offset = blk->start_offset() + blk->size();
    mu->write_to([blk](const blob &bb) { blk->add(bb); });
}

log_block *log_appender::append_empty_block()
{
    const log_block &blk = _blocks.back();
    _full_blocks_size += blk.size();
    _full_blocks_blob_cnt += blk.data().size();
    int64_t new_block_start_offset = blk.start_offset() + blk.size();
    _blocks.emplace_back(new_block_start_offset);
    return &_blocks.back();
}

/*extern*/ size_t get_sys_page_size()
{
    static const size_t PAGE_SIZE = boost::interprocess::mapped_region::get_page_size();
    return PAGE_SIZE;
}

void log_appender::finish()
{
    static const size_t PAGE_SIZE = get_sys_page_size();
    size_t bytes_size = size();

    size_t remainder = bytes_size % PAGE_SIZE;
    if (remainder == 0) {
        // no need for padding
        return;
    }
    // append a padding block
    remainder = (bytes_size + sizeof(log_block_header)) % PAGE_SIZE;
    size_t padding_len = remainder == 0 ? 0 : PAGE_SIZE - remainder;
    log_block *blk = append_empty_block();
    blk->add(blob::create_from_bytes(std::string(padding_len, '\0')));
    blk->get_log_block_header()->magic = MAGIC_PADDING_BLOCK;
}

} // namespace replication
} // namespace dsn
