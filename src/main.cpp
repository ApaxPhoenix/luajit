#include <asmjit/a64.h>
#include <iostream>
#include <chrono>

typedef int (*BinaryOpFunc)(int, int);

inline int native_add(int a, int b) {
    return a + b;
}

int main() {
    using namespace asmjit;

    JitRuntime rt;
    CodeHolder code;
    code.init(rt.environment());

    a64::Assembler a(&code);

    // Emit ARM64 machine code: W0 = W0 + W1, then return
    a.add(a64::w0, a64::w0, a64::w1);
    a.ret(a64::x30);

    BinaryOpFunc fn = nullptr;
    Error err = rt.add(&fn, &code);
    if (err != kErrorOk) {
        std::cerr << "Failed to JIT compile, error code: " << static_cast<int>(err) << "\n";
        return 1;
    }

    constexpr long long iterations = 100'000'000;

    auto start_jit = std::chrono::high_resolution_clock::now();
    long long volatile sum_jit = 0;
    for (long long i = 0; i < iterations; ++i) {
        sum_jit += fn(static_cast<int>(i), 2);
    }
    auto end_jit = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed_jit = end_jit - start_jit;

    auto start_native = std::chrono::high_resolution_clock::now();
    long long volatile sum_native = 0;
    for (long long i = 0; i < iterations; ++i) {
        sum_native += native_add(static_cast<int>(i), 2);
    }
    auto end_native = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed_native = end_native - start_native;

    std::cout << "--- Benchmark Results (" << iterations << " iterations) ---\n";
    std::cout << "JIT Function Time:   " << elapsed_jit.count() << " ms\n";
    std::cout << "Native C++ Time:     " << elapsed_native.count() << " ms\n";

    rt.release(fn);
    return 0;
}