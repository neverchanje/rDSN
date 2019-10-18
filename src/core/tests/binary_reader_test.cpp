// Copyright (c) 2017-present, Xiaomi, Inc.  All rights reserved.
// This source code is licensed under the Apache License Version 2.0, which
// can be found in the LICENSE file in the root directory of this source tree.

#include <dsn/utility/binary_reader.h>
#include <dsn/utility/binary_writer.h>
#include <boost/variant.hpp>
#include <boost/mpl/vector.hpp>
#include <boost/mpl/for_each.hpp>
#include <gtest/gtest.h>

namespace dsn {

TEST(binary_reader_test, pod_types)
{

#define POD_TYPES                                                                                  \
    int, bool, bool, uint8_t, uint16_t, uint32_t, uint64_t, int8_t, int16_t, int32_t, int64_t

    using t_variant = boost::variant<POD_TYPES>;
    t_variant test_cases[] = {
        int(0xdeadbeef),
        false,
        true,
        std::numeric_limits<uint8_t>::max(),
        std::numeric_limits<uint16_t>::max(),
        std::numeric_limits<uint32_t>::max(),
        std::numeric_limits<uint64_t>::max(),
        std::numeric_limits<int8_t>::max(),
        std::numeric_limits<int16_t>::max(),
        std::numeric_limits<int32_t>::max(),
        std::numeric_limits<int64_t>::max(),
    };

    using types_mpl_vec = boost::mpl::vector<POD_TYPES>;
    binary_writer writer;
    int i = 0;
    boost::mpl::for_each<types_mpl_vec>([&](auto arg) {
        using type = decltype(arg);
        writer.write(boost::get<type>(test_cases[i]));
        i++;
    });

    i = 0;
    binary_reader reader(writer.get_buffer());
    boost::mpl::for_each<types_mpl_vec>([&](auto arg) {
        using type = decltype(arg);
        type val;
        EXPECT_EQ(reader.read(val), sizeof(type));
        EXPECT_EQ(val, boost::get<type>(test_cases[i]));
        i++;
    });
}

TEST(binary_reader_test, two_fragments)
{
    binary_writer writer;
    writer.write(int64_t(100));
    writer.write(int64_t(100));
    ASSERT_EQ(writer.get_buffer().size(), 16);

    for (int i = 1; i < 16; i++) {
        blob lbuf = writer.get_buffer().range(0, i);
        blob rbuf = writer.get_buffer().range(i, 16 - i);

        binary_reader reader({lbuf, rbuf});
        ASSERT_EQ(reader.total_size(), 16);

        int64_t v1, v2;
        reader.read(v1);
        reader.read(v2);
        ASSERT_EQ(v1, 100) << i;
        ASSERT_EQ(v2, 100) << i;
    }
}

} // namespace dsn
