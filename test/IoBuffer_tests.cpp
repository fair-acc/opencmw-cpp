#pragma clang diagnostic push
#pragma ide diagnostic   ignored "LoopDoesntUseConditionVariableInspection"
#pragma ide diagnostic   ignored "cppcoreguidelines-avoid-magic-numbers"
#include <Debug.h>
#include <IoBuffer.h>
#include <catch2/catch.hpp>
#include <iostream>
#include <string_view>

#define N_SAMPLES 500

template<typename T>
constexpr T arraySum(const T &n) {
    return n * (n + 1) / 2;
}

TEST_CASE("IoBuffer() - constructor", "[IoBuffer]") {
    opencmw::debug::resetStats();
    {
        opencmw::debug::Timer timer("IoBuffer() - constructor", 30);
        // basic tests
        opencmw::IoBuffer  a;
        const std::size_t &cap = a.capacity();
        REQUIRE(a.capacity() == 0);
        REQUIRE(cap == a.capacity());
        REQUIRE_NOTHROW(a.reserve(100));
        REQUIRE(a.capacity() == 100);
        REQUIRE_NOTHROW(a.shrink_to_fit());
        REQUIRE(a.capacity() == 0);
    }
    REQUIRE(opencmw::debug::alloc == opencmw::debug::dealloc); // a memory leak occurred
}

TEST_CASE("IoBuffer(&other) constructor", "[IoBuffer]") {
    opencmw::debug::resetStats();
    { // test copy constructor
        opencmw::debug::Timer timer("IoBuffer(&other) constructor", 30);

        opencmw::IoBuffer     b(100);
        REQUIRE(b.size() == 0);
        REQUIRE_NOTHROW(b.put(42));
        REQUIRE(42 == b.get<int>()); // advances read position
        REQUIRE(b.position() == sizeof(int));
        REQUIRE(b.size() == sizeof(int));
        REQUIRE(b.capacity() == 100);
        opencmw::IoBuffer c(b);
        REQUIRE(c.position() == b.position());
        REQUIRE(c.capacity() == b.capacity());
        REQUIRE(c.size() == b.size());
        c.reset();
        REQUIRE(42 == c.get<int>());
        REQUIRE_NOTHROW(c.clear());
        REQUIRE(c.position() == 0);
        REQUIRE(c.size() == 0);
    }
    REQUIRE(opencmw::debug::alloc == opencmw::debug::dealloc); // a memory leak occurred
}

TEST_CASE("IoBuffer(&&other) constructor", "[IoBuffer]") {
    opencmw::debug::resetStats();
    { // test copy constructor
        opencmw::debug::Timer timer("IoBuffer(&other) constructor", 30);

        opencmw::IoBuffer     a(opencmw::IoBuffer(100));
        REQUIRE(a.size() == 0);
        REQUIRE_NOTHROW(a.put(42));
        REQUIRE(42 == a.get<int>()); // advances read position
        REQUIRE(a.position() == sizeof(int));
        REQUIRE(a.size() == sizeof(int));
        REQUIRE(a.capacity() == 100);
    }
    REQUIRE(opencmw::debug::alloc == opencmw::debug::dealloc); // a memory leak occurred
}

TEST_CASE("IoBuffer=other;", "[IoBuffer]") {
    opencmw::debug::resetStats();
    { // test copy constructor
        opencmw::debug::Timer timer("IoBuffer=other;", 30);

        opencmw::IoBuffer     b(100);
        REQUIRE(b.size() == 0);
        REQUIRE_NOTHROW(b.put(42));
        REQUIRE(42 == b.get<int>()); // advances read position
        REQUIRE(b.position() == sizeof(int));
        REQUIRE(b.size() == sizeof(int));
        REQUIRE(b.capacity() == 100);
        opencmw::IoBuffer c = b;
        REQUIRE(c.position() == b.position());
        REQUIRE(c.capacity() == b.capacity());
        REQUIRE(c.size() == b.size());
        c.reset();
        REQUIRE(42 == c.get<int>());
    }
    REQUIRE(opencmw::debug::alloc == opencmw::debug::dealloc); // a memory leak occurred
}

TEST_CASE("IoBuffer syntax - primitives", "[IoBuffer]") {
    opencmw::debug::resetStats();
    {
        opencmw::debug::Timer timer("IoBuffer syntax -prim", 30);
        opencmw::IoBuffer     a;
        a.put(static_cast<int8_t>(43));
        a.put(static_cast<int16_t>(41));
        a.put(42);
        a.put(static_cast<int64_t>(44));
        a.put(45.F);
        a.put(46.);

        // reset read position and read values
        a.reset();
        REQUIRE(43 == a.get<int8_t>());
        REQUIRE(41 == a.get<int16_t>());
        REQUIRE(42 == a.get<int>());
        REQUIRE(44 == a.get<int64_t>());
        REQUIRE(45.F == a.get<float>());
        REQUIRE(46. == a.get<double>());

        a.reset();
        short test1a = a.get<int8_t>(); // copy of IoBuffer content
        REQUIRE(43 == test1a);
        test1a = 5;
        REQUIRE(5 == test1a);
        REQUIRE(43 == a.get<int8_t>(0));
        auto &test1b = a.at<int8_t>(0); // test1b refers to actual IoBuffer memory block
        REQUIRE(43 == test1b);
        a.at<short>(0) = 5;           // '0' == index within IoBuffer
        REQUIRE(5 == a.at<short>(0)); // in-place change
        REQUIRE_THROWS_AS(a.at<short>(1000), std::out_of_range);
    }
    REQUIRE(opencmw::debug::alloc == opencmw::debug::dealloc); // a memory leak occurred
    opencmw::debug::resetStats();
}

TEST_CASE("IoBuffer bulk access", "[IoBuffer]") {
    opencmw::debug::Timer timer("IoBuffer access -bulk", 30);
    opencmw::IoBuffer     a;
    a.reserve(N_SAMPLES * sizeof(int));
    std::vector<int32_t> b(N_SAMPLES);
    std::iota(std::begin(b), std::end(b), a.size() / sizeof(int) + 1);

    a.put(b.data(), b.size());

    REQUIRE(std::accumulate(reinterpret_cast<int *>(a.data() + sizeof(int32_t)), reinterpret_cast<int *>(a.data() + a.size()), 0) == arraySum(static_cast<long>(N_SAMPLES))); // NOLINT - valid use of reinterpret_cast
}

TEST_CASE("IoBuffer by-element access", "[IoBuffer]") {
    opencmw::debug::Timer timer("IoBuffer access -element", 30);
    opencmw::IoBuffer     a;
    a.reserve(N_SAMPLES * sizeof(int));
    std::vector<int32_t> b(N_SAMPLES);

    for (int i = 0; i < 100; ++i) {
        if (a.size() % 5 == 0) {
            a.resize(0);
        }
        std::iota(std::begin(b), std::end(b), a.size() / sizeof(int) + 1);

        for (int element : b) {
            a.put(element);
        }

        REQUIRE(std::accumulate(reinterpret_cast<int *>(a.data()), reinterpret_cast<int *>(a.data() + a.size()), 0) == arraySum(static_cast<long>(a.size() / 4))); // NOLINT - valid use of reinterpret_cast
    }
}

TEST_CASE("IoBuffer syntax - string", "[IoBuffer]") {
    opencmw::debug::resetStats();
    {
        using namespace std::literals;
        opencmw::debug::Timer timer("IoBuffer syntax -string", 30);
        opencmw::IoBuffer     a;
        const std::string     helloWorld("Hello World!");
        const std::string     helloWorldUtf8("Γειά σου Κόσμε!");
        a.put("Hello World!"sv);
        a.put("Γειά σου Κόσμε!"sv);
        a.put(helloWorld);
        a.put(helloWorldUtf8);
        a.put(std::string_view("Hello World!"));
        a.put(std::string_view("Γειά σου Κόσμε!"));

        // reset read position and read values
        a.reset();
        REQUIRE("Hello World!" == a.get<std::string>());
        REQUIRE("Γειά σου Κόσμε!" == a.get<std::string>());
        REQUIRE(helloWorld == a.get<std::string>());
        REQUIRE(helloWorldUtf8 == a.get<std::string>());
        REQUIRE("Hello World!" == a.get<std::string>());
        REQUIRE("Γειά σου Κόσμε!" == a.get<std::string>());

        // reset read position and read values
        a.reset();
        REQUIRE("Hello World!" == a.get<std::string_view>());
        REQUIRE("Γειά σου Κόσμε!" == a.get<std::string_view>());
        REQUIRE(helloWorld == a.get<std::string_view>());
        REQUIRE(helloWorldUtf8 == a.get<std::string_view>());
        REQUIRE("Hello World!" == a.get<std::string_view>());
        REQUIRE("Γειά σου Κόσμε!" == a.get<std::string_view>());
    }
    REQUIRE(opencmw::debug::alloc == opencmw::debug::dealloc); // a memory leak occurred
    opencmw::debug::resetStats();
}

TEST_CASE("IoBuffer syntax - arrays", "[IoBuffer]") {
    opencmw::debug::resetStats();
    {
        using opencmw::ArrayOrVector;
        opencmw::debug::Timer timer("IoBuffer syntax -array", 30);
        opencmw::IoBuffer     buffer;
        auto                  oldBufferSize = buffer.size();

        constexpr auto        expectedSize  = []<typename T>(const T &value) {
            if constexpr (requires { value.size(); }) {
                return value.size() * sizeof(typename T::value_type) + sizeof(int32_t); // N.B. '+sizeof(int32_t)' for storing the array size
            }
            return sizeof(value) + sizeof(int32_t); // native array definition
        };
        const auto writeTest = [&buffer, &oldBufferSize, &expectedSize]<typename T, size_t size = 0>(const std::string field, const T &value) {
            const auto &msg = fmt::format("writeTest(IoBuffer&, '{}', ({})[{}])", field, opencmw::typeName<T>(), fmt::join(value, ", "));
            REQUIRE_MESSAGE(buffer.size() == oldBufferSize, msg);
            buffer.put(value);
            REQUIRE_MESSAGE((buffer.size() - oldBufferSize) == expectedSize(value), msg);
            oldBufferSize += expectedSize(value);
        };

        // add arrays
        constexpr const int arraySize           = 5;
        constexpr const int intArray[arraySize] = { 1, 2, 3, 4, 5 }; // NOLINT c-style arrays
        writeTest("intArray", intArray);

        const std::vector<int> inputIntVector(intArray, intArray + arraySize); // NOLINT not nice init via c-style array
        writeTest("inputIntVector", inputIntVector);

        constexpr const double doubleArray[5] = { 1.1, 2.2, 3.3, 4.4, 5.5 }; // NOLINT c-style arrays
        writeTest("doubleArray", doubleArray);

        const std::vector<double> inputDoubleVector(doubleArray, doubleArray + arraySize); // NOLINT not nice init via c-style array
        writeTest("inputDoubleVector", inputDoubleVector);

        std::array<int, 5> array = { 1, 2, 3, 4, 5 };
        writeTest("array", array);

        // unnamed array
        buffer.put({ 1, 2, 3, 4, 5 });

        // retrieve arrays

        // variant A: allocating a new vector
        std::vector<int> recoveredIntVector = buffer.getArray<int>();
        REQUIRE(inputIntVector == recoveredIntVector);
        REQUIRE(inputIntVector == buffer.getArray<int>(recoveredIntVector, static_cast<size_t>(5)));
        // PRINT_VECTOR(recoveredIntVector1);

        // variant B: re-using existing vector
        std::vector<double> recoveredDoubleVector;
        recoveredDoubleVector = buffer.getArray(recoveredDoubleVector, 5);
        REQUIRE(inputDoubleVector == recoveredDoubleVector); // && "error while getting double vector"
        //PRINT_VECTOR(recoveredDoubleVector);
        REQUIRE(inputDoubleVector == buffer.getArray<double>(std::vector<double>(5), 5));

        std::array<int, 5> array2 = buffer.getArray<int, 5>();
        //PRINT_VECTOR(array2);
        REQUIRE(array == array2);
        const auto         origPosition = buffer.position();
        std::array<int, 5> array3{ 0, 0, 0, 0, 0 };
        REQUIRE(array == buffer.getArray<int, 5>(array3, 5));
        //PRINT_VECTOR(array3);
        buffer.position() = origPosition; // skip back
        REQUIRE(std::array<int, 5>{ 1, 2, 3, 0, 0 } == buffer.getArray<int, 5>(std::array<int, 5>{ 0, 0, 0, 0, 0 }, 3));
    }
    REQUIRE(opencmw::debug::alloc == opencmw::debug::dealloc); // a memory leak occurred
    opencmw::debug::resetStats();
}

#pragma clang diagnostic pop