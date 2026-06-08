#include "memory_analyzer.hpp"

void print_runtime_memory(const std::string& str) {
    size_t stack_size = sizeof(str);
    size_t heap_size = 0;

    if (str.capacity() > 15) {
        heap_size = str.capacity() + 1;
    }

    std::cout << "==================================================\n";
    std::cout << "Data Type : std::string\n";
    std::cout << "Length    : " << str.length() << " (Capacity: " << str.capacity() << ")\n";
    std::cout << "Allocation: " << (str.capacity() > 15 ? "Dynamic Heap Chunk" : "Stack (SSO / Internal)") << "\n";
    std::cout << "Stack Size: " << stack_size << " bytes\n";
    std::cout << "Heap Size : " << heap_size << " bytes\n";
    std::cout << "Total RAM : " << (stack_size + heap_size) << " bytes\n";
    std::cout << "==================================================\n\n";
}

// Add this at the absolute bottom of your existing memory_analyzer.cpp file:
template void print_runtime_memory(const std::vector<char>&);