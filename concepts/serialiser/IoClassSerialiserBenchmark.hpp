#pragma clang diagnostic push
#pragma ide diagnostic   ignored "cppcoreguidelines-avoid-magic-numbers"
#ifndef OPENCMW_CPP_IOCLASSSERIALISERBENCHMARK_H
#define OPENCMW_CPP_IOCLASSSERIALISERBENCHMARK_H
#include <numeric>
#include <opencmw.hpp>

using opencmw::MultiArray;
using std::array;
using std::string;
using std::unique_ptr;
using std::vector;

struct TestDataClass;

struct TestDataClass {
    static volatile bool cmwCompatibilityMode;

    // basic data type
    bool   bool1;
    bool   bool2;
    int8_t byte1;
    int8_t byte2;
    char   char1;
    char   char2;
    short  short1;
    short  short2;
    int    int1;
    int    int2;
    long   long1;
    long   long2;
    float  float1;
    float  float2;
    double double1;
    double double2;
    string string1;
    string string2;

    // 1-dim arrays
    vector<uint8_t> boolArray;
    vector<int8_t>  byteArray;
    // vector<int8_t> charArray;
    vector<int16_t> shortArray;
    vector<int32_t> intArray;
    vector<int64_t> longArray;
    vector<float>   floatArray;
    vector<double>  doubleArray;
    vector<string>  stringArray;

    // generic n-dim arrays - N.B. striding-arrays: low-level format is the same except of 'nDimension' descriptor
    array<int, 3>  nDimensions;
    vector<char>   boolNdimArray;
    vector<int8_t> byteNdimArray;
    // MultiArray<char,2> charNdimArray;
    vector<int16_t> shortNdimArray;
    vector<int32_t> intNdimArray;
    vector<int64_t> longNdimArray;
    vector<float>   floatNdimArray;
    vector<double>  doubleNdimArray;

    // nested class
    unique_ptr<TestDataClass> nestedData;

    /**
     * @param nSizePrimitives size of primitive arrays (smaller 0: do not initialise fields/allocate arrays)
     * @param nSizeString size of String[] array (smaller 0: do not initialise fields/allocate arrays)
     * @param nestedClassRecursion how many nested sub-classes should be allocated
     */
    TestDataClass(const int nSizePrimitives = -1, const int nSizeString = -1, const int nestedClassRecursion = -1) {
        if (nestedClassRecursion > 0) {
            nestedData = std::make_unique<TestDataClass>(TestDataClass(nSizePrimitives, nSizeString, nestedClassRecursion - 1));
            nestedData->init(nSizePrimitives + 1, nSizeString + 1); //N.B. '+1' to have different sizes for nested classes
        }

        init(nSizePrimitives, nSizeString);
    }
    bool operator==(const TestDataClass &) const = default;

    void clear() {
        bool1   = false;
        bool2   = false;
        byte1   = 0;
        byte2   = 0;
        char1   = 0;
        char2   = 0;
        short1  = 0;
        short2  = 0;
        int1    = 0;
        int2    = 0;
        long1   = 0;
        long2   = 0;
        float1  = 0;
        float2  = 0;
        double1 = 0;
        double2 = 0;

        string1 = "";
        string2 = "";

        // reset 1-dim arrays
        boolArray.clear();
        byteArray.clear();
        // charArray.clear();
        shortArray.clear();
        intArray.clear();
        longArray.clear();
        floatArray.clear();
        doubleArray.clear();
        stringArray.clear();

        // reset n-dim arrays
        std::iota(nDimensions.begin(), nDimensions.end(), 0);
        boolNdimArray.clear();
        byteNdimArray.clear();
        //            charNdimArray.clear();
        shortNdimArray.clear();
        intNdimArray.clear();
        longNdimArray.clear();
        floatNdimArray.clear();
        doubleNdimArray.clear();

        //nestedData = null;
    }

    void init(const int nSizePrimitives, const int nSizeString) {
        if (nSizePrimitives >= 0) {
            bool1   = true;
            bool2   = false;
            byte1   = 10;
            byte2   = -100;
            char1   = 'a';
            char2   = 'Z';
            short1  = 20;
            short2  = -200;
            int1    = 30;
            int2    = -300;
            long1   = 40;
            long2   = -400;
            float1  = 50.5f;
            float2  = -500.5f;
            double1 = 60.6;
            double2 = -600.6;

            string1 = "Hello World!";
            string2 = "Γειά σου Κόσμε!";

            // allocate 1-dim arrays
            boolArray.resize(static_cast<std::size_t>(nSizePrimitives));
            std::generate(boolArray.begin(), boolArray.end(), []() { static int i=0; return (i++)%2==0; });
            //std::cerr << fmt::format("bool array size {} - sizeof() = {} vs {}\n", boolArray.size(), sizeof(boolArray[0]), sizeof(bool));

            byteArray.resize(static_cast<std::size_t>(nSizePrimitives));
            std::generate(byteArray.begin(), byteArray.end(), []() { static int i=1; return (i++)%127; });
            // charArray.resize(static_cast<std::size_t>(nSizePrimitives));
            // std::generate(charArray.begin(), charArray.end(), [](){ static int i=2; return (i++)%127; });
            shortArray.resize(static_cast<std::size_t>(nSizePrimitives));
            std::generate(shortArray.begin(), shortArray.end(), []() { static int i=3; return (i++)%64000; });
            intArray.resize(static_cast<std::size_t>(nSizePrimitives));
            std::generate(intArray.begin(), intArray.end(), []() { static int i=4; return i++; });
            longArray.resize(static_cast<std::size_t>(nSizePrimitives));
            std::generate(longArray.begin(), longArray.end(), []() { static int i=5; return i++; });
            floatArray.resize(static_cast<std::size_t>(nSizePrimitives));
            std::generate(floatArray.begin(), floatArray.end(), []() { static int i=6; return i++; });
            doubleArray.resize(static_cast<std::size_t>(nSizePrimitives));
            std::generate(doubleArray.begin(), doubleArray.end(), []() { static int i=7; return i++; });

            // allocate n-dim arrays -- N.B. for simplicity the dimension/low-level backing size is const

            nDimensions         = { 2, 3, 2 };
            const int nMultiDim = nDimensions[0] * nDimensions[1] * nDimensions[2];
            boolNdimArray.resize(static_cast<std::size_t>(nMultiDim));
            std::generate(boolNdimArray.begin(), boolNdimArray.end(), []() { static int i=0; return (i++)%2==0; });
            //std::cerr << fmt::format("bool array size {} - sizeof() = {} vs {}\n", boolNdimArray.size(), sizeof(boolNdimArray[0]), sizeof(bool));

            byteNdimArray.resize(static_cast<std::size_t>(nMultiDim));
            std::generate(byteNdimArray.begin(), byteNdimArray.end(), []() { static int i=1; return (i++)%127; });
            // charNdimArray.resize(static_cast<std::size_t>(nMultiDim));
            // std::generate(charNdimArray.begin(), charNdimArray.end(), [](){ static int i=2; return (i++)%127; });
            shortNdimArray.resize(static_cast<std::size_t>(nMultiDim));
            std::generate(shortNdimArray.begin(), shortNdimArray.end(), []() { static int i=3; return (i++)%64000; });
            intNdimArray.resize(static_cast<std::size_t>(nMultiDim));
            std::generate(intNdimArray.begin(), intNdimArray.end(), []() { static int i=4; return i++; });
            longNdimArray.resize(static_cast<std::size_t>(nMultiDim));
            std::generate(longNdimArray.begin(), longNdimArray.end(), []() { static int i=5; return i++; });
            floatNdimArray.resize(static_cast<std::size_t>(nMultiDim));
            std::generate(floatNdimArray.begin(), floatNdimArray.end(), []() { static int i=6; return i++; });
            doubleNdimArray.resize(static_cast<std::size_t>(nMultiDim));
            std::generate(doubleNdimArray.begin(), doubleNdimArray.end(), []() { static int i=7; return i++; });
        }
        if (nSizeString >= 0) {
            for (int i = 0; i < nSizeString; i++) {
                stringArray.emplace_back(string1);
            }
            //stringArray.resize(static_cast<std::size_t>(nSizeString));
            //std::generate(stringArray.begin(), stringArray.end(), [&]() { return string1; });
        } else {
            stringArray.clear();
        }
    }
};
REFL_CUSTOM(TestDataClass, bool1, bool2, byte1, byte2, char1, char2, short1, short2, int1, int2, long1, long2, float1, float2, double1, double2, string1, string2, // basic variables
        boolArray, byteArray, shortArray, intArray, longArray, floatArray, doubleArray, stringArray,                                                               // basic 1D-arrays
        nDimensions, boolNdimArray, byteNdimArray, shortNdimArray, intNdimArray, longNdimArray, floatNdimArray, doubleNdimArray                                    // basic ND-arrays (proxy) TODO: replace with MultiArray
                                                                                                                                                                   //, nestedData // nested class
)

volatile bool TestDataClass::cmwCompatibilityMode = false;

#endif //OPENCMW_CPP_IOCLASSSERIALISERBENCHMARK_H

#pragma clang diagnostic pop