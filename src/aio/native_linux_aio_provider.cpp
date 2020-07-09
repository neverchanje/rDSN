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

#include "native_linux_aio_provider.h"

#include <fcntl.h>
#include <cstdlib>
#include <dsn/dist/fmt_logging.h>

namespace dsn {

native_linux_aio_provider::native_linux_aio_provider(disk_engine *disk) : aio_provider(disk)
{
    memset(&_ctx, 0, sizeof(_ctx));
    auto ret = io_setup(128, &_ctx); // 128 concurrent events
    dassert(ret == 0, "io_setup error, ret = %d", ret);

    _is_running = true;
    _worker = std::thread([this, disk]() {
        task::set_tls_dsn_context(node(), nullptr);
        get_event();
    });

    _io_submit_latency.init_app_counter(
        "eon", "native_aio_submit_latency", COUNTER_TYPE_NUMBER_PERCENTILES, "");
}

native_linux_aio_provider::~native_linux_aio_provider()
{
    if (!_is_running) {
        return;
    }
    _is_running = false;

    auto ret = io_destroy(_ctx);
    dassert(ret == 0, "io_destroy error, ret = %d", ret);

    _worker.join();
}

dsn_handle_t native_linux_aio_provider::open(const char *file_name, int flag, int pmode)
{
    dsn_handle_t fh = (dsn_handle_t)(uintptr_t)::open(file_name, flag, pmode);
    if (fh == DSN_INVALID_FILE_HANDLE) {
        derror("create file failed, err = %s", strerror(errno));
    }
    return fh;
}

error_code native_linux_aio_provider::close(dsn_handle_t fh)
{
    if (fh == DSN_INVALID_FILE_HANDLE || ::close((int)(uintptr_t)(fh)) == 0) {
        return ERR_OK;
    } else {
        derror("close file failed, err = %s", strerror(errno));
        return ERR_FILE_OPERATION_FAILED;
    }
}

error_code native_linux_aio_provider::flush(dsn_handle_t fh)
{
    if (fh == DSN_INVALID_FILE_HANDLE || ::fsync((int)(uintptr_t)(fh)) == 0) {
        return ERR_OK;
    } else {
        derror("flush file failed, err = %s", strerror(errno));
        return ERR_FILE_OPERATION_FAILED;
    }
}

error_code native_linux_aio_provider::preallocate(dsn_handle_t fh, size_t offset, size_t len)
{
    if (fh == DSN_INVALID_FILE_HANDLE || fallocate((int)(uintptr_t)(fh), 0, offset, len) >= 0) {
        return ERR_OK;
    } else {
        derror("preallocate file failed, err = %s", strerror(errno));
        return ERR_FILE_OPERATION_FAILED;
    }
}

error_code native_linux_aio_provider::truncate(dsn_handle_t fh, size_t len)
{
    if (fh == DSN_INVALID_FILE_HANDLE || ftruncate((int)(uintptr_t)(fh), len) == 0) {
        return ERR_OK;
    } else {
        derror("truncate file failed, err = %s", strerror(errno));
        return ERR_FILE_OPERATION_FAILED;
    }
}

aio_context *native_linux_aio_provider::prepare_aio_context(aio_task *tsk)
{
    return new linux_disk_aio_context(tsk);
}

void native_linux_aio_provider::submit_aio_task(aio_task *aio_tsk) { aio_internal(aio_tsk, true); }

void native_linux_aio_provider::get_event()
{
    struct io_event events[1];
    int ret;

    task::set_tls_dsn_context(node(), nullptr);

    const char *name = ::dsn::tools::get_service_node_name(node());
    char buffer[128];
    sprintf(buffer, "%s.aio", name);
    task_worker::set_name(buffer);

    while (true) {
        if (dsn_unlikely(!_is_running.load(std::memory_order_relaxed))) {
            break;
        }
        ret = io_getevents(_ctx, 1, 1, events, NULL);
        if (ret > 0) // should be 1
        {
            dassert(ret == 1, "io_getevents returns %d", ret);
            /* Even though events[i].res is an unsigned number in libaio, it is
            used to return a negative value (negated errno value) to indicate
            error and a positive value to indicate number of bytes read or
            written. */
            struct iocb *io = events[0].obj;
            complete_iocb(io, static_cast<int>(events[0].res), static_cast<int>(events[0].res2));
        } else {
            // on error it returns a negated error number (the negative of one of the values listed
            // in ERRORS
            dwarn("io_getevents returns %d, you probably want to try on another machine:-(", ret);
        }
    }
}

void native_linux_aio_provider::complete_iocb(struct iocb *io, int res, int res2)
{
    linux_disk_aio_context *aio = CONTAINING_RECORD(io, linux_disk_aio_context, cb);
    error_code ec = ERR_OK;
    if (res < 0) {
        derror("aio error, res = %d, err = %s", res, strerror(-res));
        ec = ERR_FILE_OPERATION_FAILED;
    }
    if (res == 0) {
        ec = ERR_HANDLE_EOF;
    }
    dcheck_eq(res2, 0);

    if (!aio->evt) {
        aio_task *aio_ptr(aio->tsk);
        aio->this_->complete_io(aio_ptr, ec, res);
    } else {
        aio->err = ec;
        aio->bytes = res;
        aio->evt->notify();
    }
}

error_code native_linux_aio_provider::aio_internal(aio_task *aio_tsk,
                                                   bool async,
                                                   /*out*/ uint32_t *pbytes /*= nullptr*/)
{
    struct iocb *cbs[1];
    linux_disk_aio_context *aio;
    int ret;

    aio = (linux_disk_aio_context *)aio_tsk->get_aio_context();

    memset(&aio->cb, 0, sizeof(aio->cb));

    aio->this_ = this;

    switch (aio->type) {
    case AIO_Read:
        io_prep_pread(&aio->cb,
                      static_cast<int>((ssize_t)aio->file),
                      aio->buffer,
                      aio->buffer_size,
                      aio->file_offset);
        break;
    case AIO_Write:
        if (aio->buffer) {
            io_prep_pwrite(&aio->cb,
                           static_cast<int>((ssize_t)aio->file),
                           aio->buffer,
                           aio->buffer_size,
                           aio->file_offset);
        } else {
            int iovcnt = aio->write_buffer_vec->size();
            struct iovec *iov = (struct iovec *)alloca(sizeof(struct iovec) * iovcnt);
            for (int i = 0; i < iovcnt; i++) {
                const dsn_file_buffer_t &buf = aio->write_buffer_vec->at(i);
                iov[i].iov_base = buf.buffer;
                iov[i].iov_len = buf.size;
            }
            io_prep_pwritev(
                &aio->cb, static_cast<int>((ssize_t)aio->file), iov, iovcnt, aio->file_offset);
        }
        break;
    default:
        derror("unknown aio type %u", static_cast<int>(aio->type));
    }

    if (!async) {
        aio->evt = new utils::notify_event();
        aio->err = ERR_OK;
        aio->bytes = 0;
    }

    cbs[0] = &aio->cb;
    uint64_t start = dsn_now_ns();
    ret = io_submit(_ctx, 1, cbs);
    _io_submit_latency->set(dsn_now_ns() - start);

    if (ret != 1) { // must be equal to `nr` of io_submit
        if (ret < 0) {
            derror("io_submit error, ret = %d: %s", ret, strerror(-ret));
        } else { // == 0
            derror("could not submit IOs, ret = %d", ret);
        }

        if (async) {
            complete_io(aio_tsk, ERR_FILE_OPERATION_FAILED, 0);
        } else {
            delete aio->evt;
            aio->evt = nullptr;
        }
        return ERR_FILE_OPERATION_FAILED;
    } else {
        if (async) {
            return ERR_IO_PENDING;
        } else {
            aio->evt->wait();
            delete aio->evt;
            aio->evt = nullptr;
            if (pbytes != nullptr) {
                *pbytes = aio->bytes;
            }
            return aio->err;
        }
    }
}

} // namespace dsn
