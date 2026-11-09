#include "strand/ThreadPool.h"

#include <iostream>

void add(int a, int b) {
    std::cout << a+b << std::endl;
}

void cut(int a, int b) {
    std::cout << a - b << std::endl;
}

void addandcut(int a, int b) {
    std::cout << (a + b) * (a - b) << std::endl; 
}

int main() {
    my_log::ThreadPool t(3);
    t.post(add, 1, 2);
    t.post(cut, 1, 2);
    t.post(addandcut, 1, 2);
}