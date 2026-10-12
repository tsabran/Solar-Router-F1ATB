#include "harness/test_runner.h"

int main(int argc, char** argv) {
    std::string suiteFilter = "";
    if (argc > 1) {
        suiteFilter = argv[1];
    }
    return TestHarness::runAll(suiteFilter);
}
