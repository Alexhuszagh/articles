//=======================================================================
// Copyright (c) 2014 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#include <random>
#include <array>
#include <vector>
#include <list>
#include <algorithm>
#include <deque>
#include <thread>
#include <iostream>
#include <cstdint>
#include <typeinfo>
#include <memory>
#include <tuple>

#include "bench.hpp"
#include "policies.hpp"
#include "short_alloc.hpp"

namespace {

template<typename T>
constexpr bool is_trivial_of_size(std::size_t size){
    return std::is_trivial<T>::value && sizeof(T) == size;
}

template<typename T>
constexpr bool is_non_trivial_of_size(std::size_t size){
    return
            !std::is_trivial<T>::value
        &&  sizeof(T) == size
        &&  std::is_copy_constructible<T>::value
        &&  std::is_copy_assignable<T>::value
        &&  std::is_move_constructible<T>::value
        &&  std::is_move_assignable<T>::value;
}

template<typename T>
constexpr bool is_non_trivial_nothrow_movable(){
    return
            !std::is_trivial<T>::value
        &&  std::is_nothrow_move_constructible<T>::value
        &&  std::is_nothrow_move_assignable<T>::value;
}

template<typename T>
constexpr bool is_non_trivial_non_nothrow_movable(){
    return
            !std::is_trivial<T>::value
        &&  std::is_move_constructible<T>::value
        &&  std::is_move_assignable<T>::value
        &&  !std::is_nothrow_move_constructible<T>::value
        &&  !std::is_nothrow_move_assignable<T>::value;
}

template<typename T>
constexpr bool is_non_trivial_non_movable(){
    return
            !std::is_trivial<T>::value
        &&  std::is_copy_constructible<T>::value
        &&  std::is_copy_assignable<T>::value
        &&  !std::is_move_constructible<T>::value
        &&  !std::is_move_assignable<T>::value;
}

template<typename T>
constexpr bool is_small(){
   return sizeof(T) <= sizeof(std::size_t);
}

} //end of anonymous namespace

// tested types

// trivial type with parametrized size
template<int N>
struct Trivial {
    std::size_t a;
    std::array<unsigned char, N-sizeof(a)> b;
    bool operator<(const Trivial &other) const { return a < other.a; }
};

template<>
struct Trivial<sizeof(std::size_t)> {
    std::size_t a;
    bool operator<(const Trivial &other) const { return a < other.a; }
};

// non trivial, quite expensive to copy but easy to move (noexcept not set)
class NonTrivialStringMovable {
    private:
        std::string data{"some pretty long string to make sure it is not optimized with SSO"};

    public:
        std::size_t a{0};
        NonTrivialStringMovable() = default;
        NonTrivialStringMovable(std::size_t a): a(a) {}
        ~NonTrivialStringMovable() = default;
        bool operator<(const NonTrivialStringMovable &other) const { return a < other.a; }
};

// non trivial, quite expensive to copy but easy to move (with noexcept)
class NonTrivialStringMovableNoExcept {
    private:
        std::string data{"some pretty long string to make sure it is not optimized with SSO"};

    public:
        std::size_t a{0};
        NonTrivialStringMovableNoExcept() = default;
        NonTrivialStringMovableNoExcept(std::size_t a): a(a) {}
        NonTrivialStringMovableNoExcept(const NonTrivialStringMovableNoExcept &) = default;
        NonTrivialStringMovableNoExcept(NonTrivialStringMovableNoExcept &&) noexcept = default;
        ~NonTrivialStringMovableNoExcept() = default;
        NonTrivialStringMovableNoExcept &operator=(const NonTrivialStringMovableNoExcept &) = default;
        NonTrivialStringMovableNoExcept &operator=(NonTrivialStringMovableNoExcept &&other) noexcept {
            std::swap(data, other.data);
            std::swap(a, other.a);
            return *this;
        }
        bool operator<(const NonTrivialStringMovableNoExcept &other) const { return a < other.a; }
};

// non trivial, quite expensive to copy and move
template<int N>
class NonTrivialArray {
    public:
        std::size_t a = 0;

    private:
        std::array<unsigned char, N-sizeof(a)> b;

    public:
        NonTrivialArray() = default;
        NonTrivialArray(std::size_t a): a(a) {}
        ~NonTrivialArray() = default;
        bool operator<(const NonTrivialArray &other) const { return a < other.a; }
};

// type definitions for testing and invariants check
using TrivialSmall   = Trivial<8>;       static_assert(is_trivial_of_size<TrivialSmall>(8),        "Invalid type");
using TrivialMedium  = Trivial<32>;      static_assert(is_trivial_of_size<TrivialMedium>(32),      "Invalid type");
using TrivialLarge   = Trivial<128>;     static_assert(is_trivial_of_size<TrivialLarge>(128),      "Invalid type");
using TrivialHuge    = Trivial<1024>;    static_assert(is_trivial_of_size<TrivialHuge>(1024),      "Invalid type");
using TrivialMonster = Trivial<4*1024>;  static_assert(is_trivial_of_size<TrivialMonster>(4*1024), "Invalid type");

static_assert(is_non_trivial_nothrow_movable<NonTrivialStringMovableNoExcept>(), "Invalid type");
static_assert(is_non_trivial_non_nothrow_movable<NonTrivialStringMovable>(), "Invalid type");

using NonTrivialArrayMedium = NonTrivialArray<32>;
static_assert(is_non_trivial_of_size<NonTrivialArrayMedium>(32), "Invalid type");

// Define all benchmarks

template <typename T, size_t, size_t>
struct std_allocator_wrapper {
    using allocator_type = std::allocator<T>;
    allocator_type allocator;
};

template <typename T, size_t N, size_t Align>
class linear_allocator_wrapper {
public:
    using allocator_type = short_alloc<T, N, Align>;

private:
    // Define our arena type so we know to use it.
    typename short_alloc<T, N, Align>::arena_type arena_;

public:
    allocator_type allocator;

    linear_allocator_wrapper():
        arena_(),
        allocator(arena_)
    {}

    linear_allocator_wrapper(const linear_allocator_wrapper&) = delete;
    linear_allocator_wrapper& operator=(const linear_allocator_wrapper&) = delete;
};

template<typename T>
struct bench_fill_back {
    static void run(){
        new_graph<T>("fill_back", "us");

        constexpr SizesType Sizes { 100000, 200000, 300000, 400000, 500000, 600000, 700000, 800000, 900000, 1000000 };
        Bencher<std::vector, T, std_allocator_wrapper,    microseconds, Empty, Sizes, FillBack>::bench("vector");
        Bencher<std::list,   T, std_allocator_wrapper,    microseconds, Empty, Sizes, FillBack>::bench("list");
        Bencher<std::deque,  T, std_allocator_wrapper,    microseconds, Empty, Sizes, FillBack>::bench("deque");
        Bencher<std::vector, T, std_allocator_wrapper,    microseconds, Empty, Sizes, ReserveSize, FillBack>::bench("vector_reserve");
        Bencher<std::list,   T, linear_allocator_wrapper, microseconds, Empty, Sizes, FillBack>::bench("list_linear");
        Bencher<std::vector, T, std_allocator_wrapper,    microseconds, Empty, Sizes, FillBackInserter>::bench("vector_inserter");
        Bencher<std::list,   T, std_allocator_wrapper,    microseconds, Empty, Sizes, FillBackInserter>::bench("list_inserter");
        Bencher<std::deque,  T, std_allocator_wrapper,    microseconds, Empty, Sizes, FillBackInserter>::bench("deque_inserter");
        Bencher<std::list,   T, linear_allocator_wrapper, microseconds, Empty, Sizes, FillBackInserter>::bench("list_inserter_linear");
    }
};

template<typename T>
struct bench_emplace_back {
    static void run(){
        new_graph<T>("emplace_back", "us");

        constexpr SizesType Sizes { 100000, 200000, 300000, 400000, 500000, 600000, 700000, 800000, 900000, 1000000 };
        Bencher<std::vector, T, std_allocator_wrapper,    microseconds, Empty, Sizes, EmplaceBack>::bench("vector");
        Bencher<std::list,   T, std_allocator_wrapper,    microseconds, Empty, Sizes, EmplaceBack>::bench("list");
        Bencher<std::deque,  T, std_allocator_wrapper,    microseconds, Empty, Sizes, EmplaceBack>::bench("deque");
        Bencher<std::list,   T, linear_allocator_wrapper, microseconds, Empty, Sizes, EmplaceBack>::bench("list_linear");
    }
};

template<typename T>
struct bench_fill_front {
    static void run(){
        new_graph<T>("fill_front", "us");

        constexpr SizesType Sizes { 10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000, 100000 };

        // it is too slow with bigger data types
        if(is_small<T>()){
            Bencher<std::vector, T, std_allocator_wrapper, microseconds, Empty, Sizes, FillFront>::bench("vector");
        }

        Bencher<std::list,  T, std_allocator_wrapper,    microseconds, Empty, Sizes, FillFront>::bench("list");
        Bencher<std::deque, T, std_allocator_wrapper,    microseconds, Empty, Sizes, FillFront>::bench("deque");
        Bencher<std::list,  T, linear_allocator_wrapper, microseconds, Empty, Sizes, FillFront>::bench("list_linear");
    }
};

template<typename T>
struct bench_emplace_front {
    static void run(){
        new_graph<T>("emplace_front", "us");

        constexpr SizesType Sizes { 10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000, 100000 };

        // it is too slow with bigger data types
        if(is_small<T>()){
            Bencher<std::vector, T, std_allocator_wrapper, microseconds, Empty, Sizes, EmplaceFront>::bench("vector");
        }

        Bencher<std::list,  T, std_allocator_wrapper,    microseconds, Empty, Sizes, EmplaceFront>::bench("list");
        Bencher<std::deque, T, std_allocator_wrapper,    microseconds, Empty, Sizes, EmplaceFront>::bench("deque");
        Bencher<std::list,  T, linear_allocator_wrapper, microseconds, Empty, Sizes, EmplaceFront>::bench("list_linear");
    }
};

template<typename T>
struct bench_linear_search {
    static void run(){
        new_graph<T>("linear_search", "us");

        constexpr SizesType Sizes { 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000 };
        Bencher<std::vector, T, std_allocator_wrapper,    microseconds, FilledRandom, Sizes, Find>::bench("vector");
        Bencher<std::list,   T, std_allocator_wrapper,    microseconds, FilledRandom, Sizes, Find>::bench("list");
        Bencher<std::deque,  T, std_allocator_wrapper,    microseconds, FilledRandom, Sizes, Find>::bench("deque");
        Bencher<std::list,   T, linear_allocator_wrapper, microseconds, FilledRandom, Sizes, Find>::bench("list_linear");
    }
};

template<typename T>
struct bench_random_insert {
    static void run(){
        new_graph<T>("random_insert", "ms");

        constexpr SizesType Sizes { 10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000, 100000 };
        Bencher<std::vector, T, std_allocator_wrapper,    microseconds, FilledRandom, Sizes, Insert>::bench("vector");
        Bencher<std::list,   T, std_allocator_wrapper,    microseconds, FilledRandom, Sizes, Insert>::bench("list");
        Bencher<std::deque,  T, std_allocator_wrapper,    microseconds, FilledRandom, Sizes, Insert>::bench("deque");
        Bencher<std::list,   T, linear_allocator_wrapper, microseconds, FilledRandom, Sizes, Insert>::bench("list_linear");
    }
};

template<typename T>
struct bench_random_remove {
    static void run(){
        new_graph<T>("random_remove", "ms");

        constexpr SizesType Sizes { 10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000, 100000 };
        Bencher<std::vector, T, std_allocator_wrapper,    microseconds, FilledRandom, Sizes, Erase>::bench("vector");
        Bencher<std::list,   T, std_allocator_wrapper,    microseconds, FilledRandom, Sizes, Erase>::bench("list");
        Bencher<std::deque,  T, std_allocator_wrapper,    microseconds, FilledRandom, Sizes, Erase>::bench("deque");
        Bencher<std::list,   T, linear_allocator_wrapper, microseconds, FilledRandom, Sizes, Erase>::bench("list_linear");
    }
};

template<typename T>
struct bench_sort {
    static void run(){
        new_graph<T>("sort", "ms");

        constexpr SizesType Sizes { 100000, 200000, 300000, 400000, 500000, 600000, 700000, 800000, 900000, 1000000 };
        Bencher<std::vector, T, std_allocator_wrapper,    microseconds, FilledRandom, Sizes, Sort>::bench("vector");
        Bencher<std::list,   T, std_allocator_wrapper,    microseconds, FilledRandom, Sizes, Sort>::bench("list");
        Bencher<std::deque,  T, std_allocator_wrapper,    microseconds, FilledRandom, Sizes, Sort>::bench("deque");
        // Currently: GCC 10.x has a sort algorithm that default constructs
        // objects. In order to fix it, you can apply the following diff to
        // `include/bits/list.tcc`.
        // -list __carry;
        // -list __tmp[64];
        // +#define _GLIBCXX_REPEAT_8(_X) _X, _X, _X, _X, _X, _X, _X, _X
        // +list __carry(get_allocator());
        // +list __tmp[64] = {
        // +  _GLIBCXX_REPEAT_8(_GLIBCXX_REPEAT_8(list(get_allocator())))
        // +};
        Bencher<std::list,   T, linear_allocator_wrapper, microseconds, FilledRandom, Sizes, Sort>::bench("list_linear");
    }
};

template<typename T>
struct bench_destruction {
    static void run(){
        new_graph<T>("destruction", "us");

        constexpr SizesType Sizes { 100000, 200000, 300000, 400000, 500000, 600000, 700000, 800000, 900000, 1000000 };
        Bencher<std::vector, T, std_allocator_wrapper,    microseconds, SmartFilled, Sizes, SmartDelete>::bench("vector");
        Bencher<std::list,   T, std_allocator_wrapper,    microseconds, SmartFilled, Sizes, SmartDelete>::bench("list");
        Bencher<std::deque,  T, std_allocator_wrapper,    microseconds, SmartFilled, Sizes, SmartDelete>::bench("deque");
        Bencher<std::list,   T, linear_allocator_wrapper, microseconds, SmartFilled, Sizes, SmartDelete>::bench("list_linear");
    }
};

template<typename T>
struct bench_number_crunching {
    static void run(){
        new_graph<T>("number_crunching", "ms");

        constexpr SizesType Sizes { 10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000, 100000 };
        Bencher<std::vector, T, std_allocator_wrapper,    microseconds, Empty, Sizes, RandomSortedInsert>::bench("vector");
        Bencher<std::list,   T, std_allocator_wrapper,    microseconds, Empty, Sizes, RandomSortedInsert>::bench("list");
        Bencher<std::deque,  T, std_allocator_wrapper,    microseconds, Empty, Sizes, RandomSortedInsert>::bench("deque");
        Bencher<std::list,   T, linear_allocator_wrapper, microseconds, Empty, Sizes, RandomSortedInsert>::bench("list_linear");
    }
};

//Launch the benchmark

template<typename ...Types>
void bench_all(){
    bench_types<bench_fill_back,        Types...>();
    bench_types<bench_emplace_back,     Types...>();
    bench_types<bench_fill_front,       Types...>();
    bench_types<bench_emplace_front,    Types...>();
    bench_types<bench_linear_search,    Types...>();
    bench_types<bench_random_insert,    Types...>();
    bench_types<bench_random_remove,    Types...>();
    bench_types<bench_sort,             Types...>();
    bench_types<bench_destruction,      Types...>();

    // it is really slow so run only for limited set of data
    bench_types<bench_number_crunching, TrivialSmall, TrivialMedium>();
}

int main(){
    //Launch all the graphs
    bench_all<
        TrivialSmall,
        TrivialMedium,
        TrivialLarge,
        TrivialHuge,
        TrivialMonster,
        NonTrivialStringMovable,
        NonTrivialStringMovableNoExcept,
        NonTrivialArray<32>
    >();

    //Generate the graphs
    graphs::output(graphs::Output::GOOGLE);

    return 0;
}
