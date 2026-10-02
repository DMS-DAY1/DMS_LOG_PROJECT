#include "defer/defer.h"

#include <iostream>

int main() {
    int* ptr = new int(4);
    LOG_DEFER {
        if (ptr) {
            delete ptr;
            std::cout << "ptr released";
        }
    };

}