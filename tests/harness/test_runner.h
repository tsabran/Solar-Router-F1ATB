#pragma once

#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <functional>

// -----------------------------------------------------------------------------
// Framework de test ultra-léger pour C++ natif (zéro dépendance externe)
// -----------------------------------------------------------------------------

namespace TestHarness {

struct TestCase {
    std::string suite;
    std::string name;
    std::function<void()> func;
};

inline std::vector<TestCase>& getRegistry() {
    static std::vector<TestCase> registry;
    return registry;
}

inline void registerTest(const std::string& suite, const std::string& name, std::function<void()> func) {
    getRegistry().push_back({suite, name, std::move(func)});
}

struct TestFailure {
    std::string message;
    std::string file;
    int line;
};

inline int runAll(const std::string& suiteFilter = "") {
    int total = 0;
    int passed = 0;
    int failed = 0;

    std::cout << "\n=======================================================\n";
    std::cout << "       BATTERIE DE TESTS - SOLAR ROUTER F1ATB          \n";
    std::cout << "=======================================================\n\n";

    for (const auto& test : getRegistry()) {
        if (!suiteFilter.empty() && test.suite != suiteFilter) {
            continue;
        }
        total++;
        std::cout << "▶ [" << test.suite << "] " << test.name << " ... ";
        std::cout.flush();

        try {
            test.func();
            std::cout << "✅ PASS\n";
            passed++;
        } catch (const TestFailure& f) {
            std::cout << "❌ FAIL\n";
            std::cout << "   ↳ " << f.message << " (" << f.file << ":" << f.line << ")\n";
            failed++;
        } catch (const std::exception& e) {
            std::cout << "💥 EXCEPTION: " << e.what() << "\n";
            failed++;
        } catch (...) {
            std::cout << "💥 ERREUR INCONNUE\n";
            failed++;
        }
    }

    std::cout << "\n-------------------------------------------------------\n";
    std::cout << "Résultat : " << passed << " réussis, " << failed << " échoués sur " << total << " tests.\n";
    std::cout << "=======================================================\n\n";

    return (failed == 0) ? 0 : 1;
}

} // namespace TestHarness

#define TEST_CASE(suite_name, test_name) \
    static void test_##suite_name##_##test_name(); \
    namespace { \
        struct Reg_##suite_name##_##test_name { \
            Reg_##suite_name##_##test_name() { \
                TestHarness::registerTest(#suite_name, #test_name, test_##suite_name##_##test_name); \
            } \
        } s_reg_##suite_name##_##test_name; \
    } \
    static void test_##suite_name##_##test_name()

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            throw TestHarness::TestFailure{"Assertion échouée: " #cond, __FILE__, __LINE__}; \
        } \
    } while(0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ(expected, actual) \
    do { \
        if (!((expected) == (actual))) { \
            std::ostringstream _oss; \
            _oss << "Attendu: " << (expected) << ", Reçu: " << (actual) << " (" #expected " == " #actual ")"; \
            throw TestHarness::TestFailure{_oss.str(), __FILE__, __LINE__}; \
        } \
    } while(0)

#define ASSERT_NEAR(expected, actual, epsilon) \
    do { \
        if (std::fabs((double)(expected) - (double)(actual)) > (double)(epsilon)) { \
            std::ostringstream _oss; \
            _oss << "Attendu ~" << (expected) << " (±" << (epsilon) << "), Reçu: " << (actual); \
            throw TestHarness::TestFailure{_oss.str(), __FILE__, __LINE__}; \
        } \
    } while(0)
