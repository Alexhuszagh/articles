//=======================================================================
// Copyright (c) 2014 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#include <chrono>
#include <tuple>

#include "graphs.hpp"
#include "demangle.hpp"

// chrono typedefs

using std::chrono::milliseconds;
using std::chrono::microseconds;

using Clock = std::chrono::high_resolution_clock;

// Number of repetitions of each test

static const std::size_t REPEAT = 7;

// variadic policy runner

template<class Container>
inline static void run(Container &, std::size_t){
    //End of recursion
}

template<template<class> class Test, template<class> class ...Rest, class Container>
inline static void run(Container &container, std::size_t size){
    Test<Container>::run(container, size);
    run<Rest...>(container, size);
}

// A struct of our sizes, so we can define them in a template.
struct SizesType {
    size_t i0;
    size_t i1;
    size_t i2;
    size_t i3;
    size_t i4;
    size_t i5;
    size_t i6;
    size_t i7;
    size_t i8;
    size_t i9;
};

// benchmarking procedure
template<template <typename...> typename Container,
         typename ValueType,
         template <typename, size_t, size_t> typename AllocatorWrapper,
         typename DurationUnit,
         template <typename> typename CreatePolicy,
         SizesType Sizes,
         template <typename> typename ...TestPolicy
        >
struct Bencher {
    template <size_t N>
    struct functor {
        void operator()(const std::string& type) {
            // Assume we're doing a doubly-linked list.
            // This isn't guaranteed to be the correct size,
            // but it works for Clang and GCC.
            constexpr size_t NodeSize = sizeof(ValueType) + 2 * sizeof(uintptr_t);
            // We can insert up to 1000 elements.
            constexpr size_t BufferSize = sizeof(NodeSize) * (N + 1000);
            constexpr size_t Align = alignof(NodeSize);
            using WrapperType = AllocatorWrapper<ValueType, BufferSize, Align>;
            using ContainerType = Container<ValueType, typename WrapperType::allocator_type>;

            std::size_t duration = 0;

            for(std::size_t i=0; i < REPEAT; ++i) {
                auto wrapper = WrapperType();
                auto container = CreatePolicy<ContainerType>::make(N, wrapper.allocator);

                Clock::time_point t0 = Clock::now();

                run<TestPolicy...>(container, N);

                Clock::time_point t1 = Clock::now();
                duration += std::chrono::duration_cast<DurationUnit>(t1 - t0).count();
            }

            graphs::new_result(type, std::to_string(N), duration / REPEAT);
            CreatePolicy<ContainerType>::clean();
        }
    };

    static void bench(const std::string& type) {
        functor<Sizes.i0>()(type);
        functor<Sizes.i1>()(type);
        functor<Sizes.i2>()(type);
        functor<Sizes.i3>()(type);
        functor<Sizes.i4>()(type);
        functor<Sizes.i5>()(type);
        functor<Sizes.i6>()(type);
        functor<Sizes.i7>()(type);
        functor<Sizes.i8>()(type);
        functor<Sizes.i9>()(type);
    }

};

template<template<class> class Benchmark>
void bench_types(){
    //Recursion end
}

template<template<class> class Benchmark, typename T, typename ...Types>
void bench_types(){
    Benchmark<T>::run();
    bench_types<Benchmark, Types...>();
}

bool is_tag(int c){
    return std::isalnum(c) || c == '_';
}

std::string tag(std::string name){
    std::replace_if(begin(name), end(name), [](char c){ return !is_tag(c); }, '_');
    std::string res;
    res.swap(name);
    return res;
}

template<typename T>
void new_graph(const std::string &testName, const std::string &unit){
    std::string title(testName + " - " + demangle(typeid(T).name()));
    graphs::new_graph(tag(title), title, unit);
}
