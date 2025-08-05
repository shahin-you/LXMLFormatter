/*
    * Tiny Unit Test Framework
    * Version: 1.0.0
    * Author: Shahin Youssefi
    * License: MIT
    * Date: 2025-08-03
    * 
    * Lightweight header-only (C++17) testing framework with no external dep
    *   -----------------------------------------------------------------
    *   HOW TO USE
    *   -----------------------------------------------------------------
    *   1.  Include this header **once** in your test executable:
    *       #include "UnitTestFramework.h"
    *   2.  Write tests with the `TEST_CASE(MyTestName)` macro and the
    *       `REQUIRE`, `REQUIRE_EQ`, or `REQUIRE_NE` assertions.
    *   3.  Compile all test translation units into a single binary.  The
    *       `main()` provided at the bottom of this file will automatically
    *       discover and run every test that registers itself via the macro.
    *   4.  Run the binary; the framework prints a succinct pass/fail summary
    *       and returns a `non-zero` exit status if any test fails (ideal for
    *       CI pipelines).
    *   -----------------------------------------------------------------
    *   Design philosophy
    *   -----------------------------------------------------------------
    *  Header-only & self-contained: just drop the file in `tests/` and
    *        you are done.  No build-system gymnastics, no external downloads.
    *   Tiny surface: a handful of macros + one exception type. Enough
    *       for expressive assertions without the complexity of a full testing
    *       library.
    *   Zero dynamic allocation in hot paths: the only `new` happens in
    *       `registry()` when a test is registered at `static-initialisation`
    *       time.  Running tests themselves incurs no allocations.
    *    -----------------------------------------------------------------
*/

#pragma once
#include <iostream>
#include <sstream>
#include <vector>
#include <exception>
#include <utility>

namespace ut {

    //------------------------------------------------------------------
    // struct failure
    //------------------------------------------------------------------
    /// @brief  Exception thrown when an assertion (`REQUIRE*`) fails.
    ///
    /// `failure` stores the formatted diagnostic message that gets printed
    /// by the test runner. Catching a specialised type (instead of the
    /// broader `std::exception`) lets us cleanly separate *expected* test
    /// failures from *unexpected* runtime errors.
    struct failure : std::exception {
        std::string msg;        /// Human‑readable error message.
        explicit failure(std::string m) : msg(std::move(m)) {} /// Construct from a `std::string`
        const char* what() const noexcept override { return msg.c_str(); } /// Retrieve the stored message
    };

    //------------------------------------------------------------------
    // type alias test_fn
    //------------------------------------------------------------------
    /// @brief  Pointer to a void() function that contains a test body.
    using test_fn = void (*)();

    //------------------------------------------------------------------
    // registry()
    //------------------------------------------------------------------
    /// @brief  Global vector that owns all registered tests.
    ///
    /// The function returns a reference, allowing both registration (push)
    /// during static initialisation *and* iteration later when `main()`
    /// enumerates tests. Implementation chosen over a global variable so
    /// that the sequencing of static initialisation is well‑defined.
    inline std::vector<std::pair<const char*, test_fn>>& registry() {
        static std::vector<std::pair<const char*, test_fn>> tests;
        return tests;
    }

    //------------------------------------------------------------------
    // struct registrar
    //------------------------------------------------------------------
    /// @brief  Helper whose constructor inserts a test into `registry()`.
    ///
    /// One instance of `registrar` is declared *inside* each `TEST_CASE`
    /// macro expansion; this happens at static‑initialisation time, long
    /// before `main()` begins.  The result is seamless auto‑registration of
    /// every test without any user bookkeeping.    
    struct registrar {
        registrar(const char* name, test_fn fn) { registry().emplace_back(name, fn); }
    };

//------------------------------------------------------------------
// Helper macros
//------------------------------------------------------------------

#define UT_CONCAT_(a, b) a##b
#define UT_CONCAT(a, b)  UT_CONCAT_(a, b)

//------------------------------------------------------------------
//  TEST_CASE
//------------------------------------------------------------------
/// @brief  Define a new test function.
///
/// Usage:
/// ```cpp
/// TEST_CASE(MyFancyTest) {
///     REQUIRE_EQ(1 + 1, 2);
/// }
/// ```
/// The macro expands to:
///   * a forward declaration of the test body;
///   * a *static* `ut::registrar` instance that auto‑registers the test;
///   * the test body implementation.
#define TEST_CASE(name)                                           \
    static void name();                                           \
    static ut::registrar UT_CONCAT(_reg_, __LINE__){#name, name}; \
    static void name()

//------------------------------------------------------------------
//  REQUIRE & convenience wrappers
//------------------------------------------------------------------
/// @brief  Assert that `expr` is truthy; otherwise throw `ut::failure`.
///
/// The macro captures the *source location* (file & line) in the error
/// message so that a failing test is easy to track down.
#define REQUIRE(expr)                                                                \
    do {                                                                             \
        if (!(expr)) {                                                               \
            std::ostringstream _oss;                                                 \
            _oss << __FILE__ << ':' << __LINE__ << " REQUIRE failed: " << #expr;    \
            throw ut::failure{_oss.str()};                                           \
        }                                                                            \
    } while (false)

#define REQUIRE_EQ(a, b) REQUIRE((a) == (b))
#define REQUIRE_NE(a, b) REQUIRE((a) != (b))

}  //namespace ut

int main() {
    int passed = 0, failed = 0;
    for (auto& [name, fn] : ut::registry()) {
        try {
            std::cout << "Running " << name << " ... ";
            fn();
            ++passed;
            std::cout << "[DONE]" << std::endl;
        } catch (const ut::failure& f) {
            std::cerr << "[FAIL] " << name << ": " << f.what() << '\n';
            ++failed;
        } catch (const std::exception& ex) {
            std::cerr << "[ERROR] " << name << ": unexpected exception: " << ex.what() << '\n';
            ++failed;
        }
    }
    std::cout << passed << " tests passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
