#pragma once

#include <cstring>
#include <dsn/utility/blob.h>
#include <dsn/utility/string_view.h>

namespace dsn {

/// A non-continuous constant buffer that can be read into an array of values.
/// \inherit dsn::rpc_read_stream
class binary_reader
{
public:
    explicit binary_reader(blob blob);

    explicit binary_reader(std::vector<blob> fragments);

    virtual ~binary_reader() = default;

    /// \return how many bytes being read
    template <typename T>
    int read(/*out*/ T &val)
    {
        // read of this type is not implemented
        assert(false);
        return 0;
    }
    int read(/*out*/ int8_t &val) { return read_pod(val); }
    int read(/*out*/ uint8_t &val) { return read_pod(val); }
    int read(/*out*/ int16_t &val) { return read_pod(val); }
    int read(/*out*/ uint16_t &val) { return read_pod(val); }
    int read(/*out*/ int32_t &val) { return read_pod(val); }
    int read(/*out*/ uint32_t &val) { return read_pod(val); }
    int read(/*out*/ int64_t &val) { return read_pod(val); }
    int read(/*out*/ uint64_t &val) { return read_pod(val); }
    int read(/*out*/ bool &val) { return read_pod(val); }

    int read(/*out*/ std::string &s);
    int read(char *buffer, int sz);
    int read(blob &blob);
    int read(blob &blob, int len);

    template <typename T>
    int read_pod(/*out*/ T &val)
    {
        if (sizeof(T) <= get_remaining_size()) {
            auto value_buf = reinterpret_cast<char *>(&val);
            read(value_buf, sizeof(T));
            return sizeof(T);
        } else {
            // read beyond the end of buffer
            assert(false);
            return 0;
        }
    }

    blob get_buffer() const
    {
        assert(_fragments.size() == 1);
        return _fragments[0];
    }
    bool is_eof() const { return _remaining_size == 0; }
    int total_size() const { return _size; }
    int get_remaining_size() const { return _remaining_size; }

protected:
    binary_reader() = default;
    void init(blob bb);
    void init(std::vector<blob> fragments);

private:
    std::vector<blob> _fragments;
    string_view _front_ref;
    size_t _ref_idx{0}; // the index of _front_ref in _fragments

    size_t _size{0};
    size_t _remaining_size{0};
};

} // namespace dsn
