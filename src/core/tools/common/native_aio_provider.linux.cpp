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

#include "native_aio_provider.linux.h"

#include <fcntl.h>
#include <cstdlib>
#include <dsn/utility/smart_pointers.h>

namespace dsn {
namespace tools {

native_linux_aio_provider::native_linux_aio_provider(disk_engine *disk,
                                                     aio_provider *inner_provider)
    : aio_provider(disk, inner_provider)
{
    memset(&_ctx, 0, sizeof(_ctx));
    auto ret = io_setup(128, &_ctx); // 128 concurrent events
    dassert(ret == 0, "io_setup error, ret = %d", ret);
}

native_linux_aio_provider::~native_linux_aio_provider()
{
    if (!_is_running) {
        return;
    }
    _is_running = false;

    int ret = io_destroy(_ctx);
    dassert(ret == 0, "io_destroy error, ret = %d", ret);

    _worker.join();
}

void native_linux_aio_provider::start()
{
    _is_running = true;
    _worker = std::thread([this]() {
        task::set_tls_dsn_context(node(), nullptr);
        get_event();
    });
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

void native_linux_aio_provider::aio(aio_task *aio_tsk) { aio_internal(aio_tsk); }

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
        // int io_getevents(aio_context_t ctx_id, long min_nr, long nr,
        //                  struct io_event *events, struct timespec *timeout);
        // - min_nr = 1
        // - nr = 1
        // - timeout = NULL
        // Reads at least as well as up to 1 event from completion queue of AIO.
        // Blocks indefinitely until 1 read event obtained.
        ret = io_getevents(_ctx, 1, 1, events, NULL);
        if (ret > 0) // should be 1
        {
            dassert(ret == 1, "io_getevents returns %d", ret);
            complete_aio(reinterpret_cast<linux_disk_aio_context *>(events[0].data),
                         events[0].res,
                         static_cast<int>(events[0].res2));
        } else {
            // If the returned number is less than 1, it means an OS interruption occurred.
            // Check http://man7.org/linux/man-pages/man2/io_getevents.2.html#ERRORS.
            dwarn("io_getevents returns %d", ret);
        }
    }
}

void native_linux_aio_provider::complete_aio(linux_disk_aio_context *linux_ctx,
                                             int64_t bytes,
                                             int err)
{
    error_code ec;
    if (err != 0) {
        derror("aio error, err = %s", strerror(err));
        ec = ERR_FILE_OPERATION_FAILED;
    } else {
        ec = bytes > 0 ? ERR_OK : ERR_HANDLE_EOF;
    }

    if (!linux_ctx->evt) {
        dassert(linux_ctx->async, "this AIO task must in async mode");
        complete_io(linux_ctx->tsk, ec, bytes);
        delete linux_ctx;
    } else {
        dassert(!linux_ctx->async, "this AIO task must in sync mode");
        linux_ctx->err = ec;
        linux_ctx->bytes = bytes;
        linux_ctx->evt->notify();
    }
}

error_code native_linux_aio_provider::aio_internal(aio_task *aio_tsk,
                                                   bool async /*= true*/,
                                                   /*out*/ int64_t *pbytes /*= nullptr*/)
{
    struct iocb cb;
    int ret;

    auto linux_ctx = dsn::make_unique<linux_disk_aio_context>();
    linux_ctx->async = async;
    linux_ctx->tsk = aio_tsk;
    aio_context *aio = aio_tsk->get_aio_context();

    switch (aio->type) {
    case AIO_Read:
        io_prep_pread(&cb,
                      static_cast<int>((ssize_t)aio->file),
                      aio->buffer,
                      aio->buffer_size,
                      aio->file_offset);
        break;
    case AIO_Write:
        if (aio->buffer) {
            io_prep_pwrite(&cb,
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
                &cb, static_cast<int>((ssize_t)aio->file), iov, iovcnt, aio->file_offset);
        }
        break;
    default:
        derror("unknown aio type %u", static_cast<int>(aio->type));
    }

    if (!async) {
        // linux_ctx->evt is only created on non-asynchronous mode.
        linux_ctx->evt = dsn::make_unique<utils::notify_event>();
        linux_ctx->err = ERR_OK;
        linux_ctx->bytes = 0;
    }
    cb.data = linux_ctx.get();

    // Submits 1 AIO task.
    struct iocb *cbs = &cb;
    ret = io_submit(_ctx, 1, &cbs);
    if (ret != 1) {
        // If the submitted iocb is less than 1
        derror("io_submit error, ret = %d", ret);
        if (async) {
            complete_io(aio_tsk, ERR_FILE_OPERATION_FAILED, 0);
        } else {
            linux_ctx->evt.reset();
        }
        return ERR_FILE_OPERATION_FAILED;
    } else {
        if (async) {
            // Because `linux_ctx` will be taken out from the aio-getevent thread,
            // it will be deleted then.
            linux_ctx.release();
            // Success. Means this task is pending in the wait queue.
            return ERR_IO_PENDING;
        } else {
            linux_ctx->evt->wait();
            linux_ctx->evt.reset();
            if (pbytes != nullptr) {
                *pbytes = linux_ctx->bytes;
            }
            return linux_ctx->err;
        }
    }
}

} // namespace tools
} // namespace dsn
