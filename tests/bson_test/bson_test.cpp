#include "glaze/glaze.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <variant>
#include <optional>
#include <array>
#include <deque>
#include <list>
#include <random>

#include "ut/ut.hpp"

using namespace ut;

struct simple_struct {
    int32_t i = 1;
    double d = 2.2;
    std::string s = "hello";

    bool operator==(const simple_struct&) const = default;
};

template <>
struct glz::meta<simple_struct> {
    using T = simple_struct;
    static constexpr auto value = object(
        "i", &T::i,
        "d", &T::d,
        "s", &T::s
    );
};

struct nested_struct {
    simple_struct inner;
    std::vector<int32_t> v = {1, 2, 3};

    bool operator==(const nested_struct&) const = default;
};

template <>
struct glz::meta<nested_struct> {
    using T = nested_struct;
    static constexpr auto value = object(
        "inner", &T::inner,
        "v", &T::v
    );
};

enum class my_enum {
    A, B, C
};

template <>
struct glz::meta<my_enum> {
    using enum my_enum;
    static constexpr auto value = enumerate(A, B, C);
};

struct all_types_struct {
    int8_t i8 = 8;
    int16_t i16 = 16;
    int32_t i32 = 32;
    int64_t i64 = 64;
    uint8_t u8 = 80;
    uint16_t u16 = 160;
    uint32_t u32 = 320;
    uint64_t u64 = 640;
    float f = 3.14f;
    double d = 2.718;
    bool b = true;
    std::string s = "BSON test";
    std::vector<int> v = {1, 2, 3};
    std::map<std::string, double> m = {{"pi", 3.14}, {"e", 2.71}};
    my_enum e = my_enum::B;
    std::optional<int> o1 = 42;
    std::optional<int> o2 = std::nullopt;

    bool operator==(const all_types_struct&) const = default;
};

template <>
struct glz::meta<all_types_struct> {
    using T = all_types_struct;
    static constexpr auto value = object(
        "i8", &T::i8,
        "i16", &T::i16,
        "i32", &T::i32,
        "i64", &T::i64,
        "u8", &T::u8,
        "u16", &T::u16,
        "u32", &T::u32,
        "u64", &T::u64,
        "f", &T::f,
        "d", &T::d,
        "b", &T::b,
        "s", &T::s,
        "v", &T::v,
        "m", &T::m,
        "e", &T::e,
        "o1", &T::o1,
        "o2", &T::o2
    );
};

struct global_empty {};
template <> struct glz::meta<global_empty> { static constexpr auto value = glz::object(); };

int main() {
    "simple_struct"_test = [] {
        simple_struct s{42, 3.14, "world"};
        std::string buffer;
        expect(!glz::write_bson(s, buffer));
        simple_struct s2;
        expect(!glz::read_bson(s2, buffer));
        expect(s == s2);
    };

    "nested_struct"_test = [] {
        nested_struct n{{10, 1.1, "nested"}, {4, 5, 6}};
        std::string buffer;
        expect(!glz::write_bson(n, buffer));
        nested_struct n2;
        expect(!glz::read_bson(n2, buffer));
        expect(n == n2);
    };

    "all_types"_test = [] {
        all_types_struct s;
        std::string buffer;
        expect(!glz::write_bson(s, buffer));
        all_types_struct s2;
        expect(!glz::read_bson(s2, buffer));
        expect(s == s2);
    };

    "empty_string"_test = [] {
        std::string s = "";
        std::string buffer;
        expect(!glz::write_bson(s, buffer));
        std::string s2;
        expect(!glz::read_bson(s2, buffer));
        expect(s == s2);
    };

    "long_string"_test = [] {
        std::string s(10000, 'x');
        std::string buffer;
        expect(!glz::write_bson(s, buffer));
        std::string s2;
        expect(!glz::read_bson(s2, buffer));
        expect(s == s2);
    };

    "vector_int"_test = [] {
        std::vector<int> v = {1, 2, 3, 4, 5};
        std::string buffer;
        expect(!glz::write_bson(v, buffer));
        std::vector<int> v2;
        expect(!glz::read_bson(v2, buffer));
        expect(v == v2);
    };

    "vector_string"_test = [] {
        std::vector<std::string> v = {"a", "b", "c"};
        std::string buffer;
        expect(!glz::write_bson(v, buffer));
        std::vector<std::string> v2;
        expect(!glz::read_bson(v2, buffer));
        expect(v == v2);
    };

    "map_string_int"_test = [] {
        std::map<std::string, int> m = {{"one", 1}, {"two", 2}};
        std::string buffer;
        expect(!glz::write_bson(m, buffer));
        std::map<std::string, int> m2;
        expect(!glz::read_bson(m2, buffer));
        expect(m == m2);
    };

    "variant_int_string"_test = [] {
        using V = std::variant<int, std::string>;
        V v = 42;
        std::string buffer;
        expect(!glz::write_bson(v, buffer));
        V v2;
        expect(!glz::read_bson(v2, buffer));
        expect(v == v2);

        v = "hello";
        buffer.clear();
        expect(!glz::write_bson(v, buffer));
        expect(!glz::read_bson(v2, buffer));
        expect(v == v2);
    };

    "deque_double"_test = [] {
        std::deque<double> d = {1.1, 2.2, 3.3};
        std::string buffer;
        expect(!glz::write_bson(d, buffer));
        std::deque<double> d2;
        expect(!glz::read_bson(d2, buffer));
        expect(d == d2);
    };

    "list_bool"_test = [] {
        std::list<bool> l = {true, false, true};
        std::string buffer;
        expect(!glz::write_bson(l, buffer));
        std::list<bool> l2;
        expect(!glz::read_bson(l2, buffer));
        expect(l == l2);
    };

    "array_int_3"_test = [] {
        std::array<int, 3> a = {1, 2, 3};
        std::string buffer;
        expect(!glz::write_bson(a, buffer));
        std::array<int, 3> a2;
        expect(!glz::read_bson(a2, buffer));
        expect(a == a2);
    };

    "random_ints"_test = [] {
        std::mt19937 gen(42);
        std::uniform_int_distribution<int32_t> dist(-1000000, 1000000);
        for (int i = 0; i < 50; ++i) {
            int32_t val = dist(gen);
            std::string buffer;
            expect(!glz::write_bson(val, buffer));
            int32_t val2;
            expect(!glz::read_bson(val2, buffer));
            expect(val == val2);
        }
    };

    "random_doubles"_test = [] {
        std::mt19937 gen(43);
        std::uniform_real_distribution<double> dist(-1000000.0, 1000000.0);
        for (int i = 0; i < 50; ++i) {
            double val = dist(gen);
            std::string buffer;
            expect(!glz::write_bson(val, buffer));
            double val2;
            expect(!glz::read_bson(val2, buffer));
            expect(val == val2);
        }
    };

    "empty_object"_test = [] {
        global_empty e;
        std::string buffer;
        expect(!glz::write_bson(e, buffer));
        global_empty e2;
        expect(!glz::read_bson(e2, buffer));
    };

    "empty_array"_test = [] {
        std::vector<int> v;
        std::string buffer;
        expect(!glz::write_bson(v, buffer));
        std::vector<int> v2;
        expect(!glz::read_bson(v2, buffer));
        expect(v == v2);
    };

    "nested_empty"_test = [] {
        std::vector<std::vector<std::map<std::string, int>>> v = {{ {}, {} }, {}};
        std::string buffer;
        expect(!glz::write_bson(v, buffer));
        std::vector<std::vector<std::map<std::string, int>>> v2;
        expect(!glz::read_bson(v2, buffer));
        expect(v == v2);
    };

    return 0;
}
