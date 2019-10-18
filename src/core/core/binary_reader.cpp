#include <dsn/utility/utils.h>
#include <dsn/utility/binary_reader.h>
#include <dsn/c/api_utilities.h>

namespace dsn {

binary_reader::binary_reader(blob bb) { init(std::move(bb)); }

binary_reader::binary_reader(std::vector<blob> fragments)
{
    assert(!fragments.empty()); // no data to read
    init(std::move(fragments));
}

void binary_reader::init(blob bb) { init(std::vector<blob>({std::move(bb)})); }

void binary_reader::init(std::vector<blob> fragments)
{
    _fragments = std::move(fragments);
    _front_ref = _fragments[0];
    _size = 0;
    for (const blob &frag : _fragments) {
        _size += frag.length();
    }
    _remaining_size = _size;
}

int binary_reader::read(/*out*/ std::string &s)
{
    int len;
    if (0 == read(len))
        return 0;

    s.resize(len, 0);

    if (len > 0) {
        int x = read(&s[0], len);
        return x == 0 ? x : (x + sizeof(len));
    } else {
        return static_cast<int>(sizeof(len));
    }
}

int binary_reader::read(blob &blob)
{
    int len;
    if (0 == read(len))
        return 0;
    return read(blob, len);
}

int binary_reader::read(blob &blob, int len)
{
    if (blob.length() < len) { // expand the capacity
        blob = blob::create_empty(len);
    }
    return read(blob.mutable_data(), len);
}

int binary_reader::read(char *buffer, int sz)
{
    assert(get_remaining_size() >= sz);

    int remained = sz;
    while (remained > 0) {
        if (_front_ref.empty()) {
            assert(_ref_idx + 1 < _fragments.size());
            _front_ref = _fragments[++_ref_idx]; // switch to next fragment
        }
        size_t read_sz;
        if (_front_ref.size() >= remained) { // the fragment has enough data to read
            read_sz = remained;
        } else {
            read_sz = _front_ref.size();
        }
        memcpy(buffer, _front_ref.data(), read_sz);
        _front_ref.remove_prefix(read_sz);
        _remaining_size -= read_sz;
        buffer += read_sz;
        remained -= read_sz;
    }
    return sz;
}

} // namespace dsn
