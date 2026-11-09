#define FMT_HEADER_ONLY

 #include "fmt/core.h"

 int main() {
    int success = 1;
    fmt::print("success{}", success);
 }