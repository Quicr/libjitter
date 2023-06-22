#include <doctest/doctest.h>
#include "JitterBuffer.hh"

TEST_CASE("libjitter::construct")
{
    CHECK_EQ(1,1);
}

TEST_CASE("libjitter::concealment")
{
    // Provide seq 1, then 3. Should callback with 2.
    const JitterBuffer buffer = JitterBuffer(1, 1, 100, 10);
}