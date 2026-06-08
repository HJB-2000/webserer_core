#ifndef MEMORY_ANALYZER_HPP
#define MEMORY_ANALYZER_HPP

#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <typeinfo>
#include <stdlib.h>

#ifdef __GNUG__
#include <cxxabi.h>
#endif

// =========================================================================
// 1. Demangling Helper (Stays in header)
// =========================================================================
template <typename T>
std::string get_type_name() {
    const char* mangled = typeid(T).name();
#ifdef __GNUG__
    int status = 0;
    char* demangled = abi::__cxa_demangle(mangled, NULL, NULL, &status);
    if (status == 0 && demangled != NULL) {
        std::string result(demangled);
        free(demangled);
        return result;
    }
#endif
    return std::string(mangled);
}

// =========================================================================
// 2. Pure Prototype for Concrete String Function
// =========================================================================
void print_runtime_memory(const std::string& str);

// =========================================================================
// 3. Vector Template Implementation
// =========================================================================
template <typename T>
void print_runtime_memory(const std::vector<T>& vec) {
    size_t stack_size = sizeof(vec);
    size_t heap_size = vec.capacity() * sizeof(T);
    
    std::cout << "==================================================\n";
    std::cout << "Data Type : " << get_type_name<std::vector<T> >() << "\n";
    std::cout << "Elements  : " << vec.size() << " (Capacity: " << vec.capacity() << ")\n";
    std::cout << "Stack Size: " << stack_size << " bytes (Control Block)\n";
    std::cout << "Heap Size : " << heap_size << " bytes (Contiguous Buffer)\n";
    std::cout << "Total RAM : " << (stack_size + heap_size) << " bytes\n";
    std::cout << "==================================================\n\n";
}

// =========================================================================
// 4. Map Template Implementation
// =========================================================================
template <typename K, typename V>
void print_runtime_memory(const std::map<K, V>& m) {
    size_t stack_size = sizeof(m);

    struct RedBlackNode {
        void* parent;
        void* left;
        void* right;
        int color; 
        std::pair<const K, V> value;
    };

    size_t single_node_size = sizeof(RedBlackNode);
    size_t heap_size = m.size() * single_node_size;

    std::cout << "==================================================\n";
    std::cout << "Data Type : " << get_type_name<std::map<K, V> >() << "\n";
    std::cout << "Elements  : " << m.size() << " Nodes\n";
    std::cout << "Stack Size: " << stack_size << " bytes\n";
    std::cout << "Heap Size : ~" << heap_size << " bytes\n";
    std::cout << "Node Cost : ~" << single_node_size << " bytes per element\n";
    std::cout << "Total RAM : ~" << (stack_size + heap_size) << " bytes\n";
    std::cout << "==================================================\n\n";
}

#endif // MEMORY_ANALYZER_HPP