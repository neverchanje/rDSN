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

#define POD_TYPES                                                                                  \
    int, bool, bool, uint8_t, uint16_t, uint32_t, uint64_t, int8_t, int16_t, int32_t, int64_t
using t_variant = boost::variant<POD_TYPES>;
 
template<typename T>
void test_reader(binary_reader &reader, t_variant& var) {
    T val;
    EXPECT_EQ(reader.read(val), sizeof(T));
    EXPECT_EQ(val, boost::get<T>(var));
}

TEST(binary_reader_test, pod_types)
{
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

    binary_writer writer;
    writer.write(boost::get<int>(test_cases[0]));
    writer.write(boost::get<bool>(test_cases[1]));
    writer.write(boost::get<bool>(test_cases[2]));
    writer.write(boost::get<uint8_t>(test_cases[3]));
    writer.write(boost::get<uint16_t>(test_cases[4]));
    writer.write(boost::get<uint32_t>(test_cases[5]));
    writer.write(boost::get<uint64_t>(test_cases[6]));
    writer.write(boost::get<int8_t>(test_cases[7]));
    writer.write(boost::get<int16_t>(test_cases[8]));
    writer.write(boost::get<int32_t>(test_cases[9]));
    writer.write(boost::get<int64_t>(test_cases[10]));

    binary_reader reader(writer.get_buffer());
    test_reader<int>(reader, test_cases[0]);
    test_reader<bool>(reader, test_cases[1]);
    test_reader<bool>(reader, test_cases[2]);
    test_reader<uint8_t>(reader, test_cases[3]);
    test_reader<uint16_t>(reader, test_cases[4]);
    test_reader<uint32_t>(reader, test_cases[5]);
    test_reader<uint64_t>(reader, test_cases[6]);
    test_reader<int8_t>(reader, test_cases[7]);
    test_reader<int16_t>(reader, test_cases[8]);
    test_reader<int32_t>(reader, test_cases[9]);
    test_reader<int64_t>(reader, test_cases[10]);
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
