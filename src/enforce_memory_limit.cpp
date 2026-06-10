#include "Headers/EventLoop.hpp"
#include "serverConfig.hpp"
#include "Headers/API_conf.hpp"
#include "Headers/Logger.hpp"

#include <iostream>
#include <fstream>
#include <unistd.h>
#include <cstdlib>

const size_t MEMORY_LIMIT_BYTES = 2ULL * 1024 * 1024 * 1024;

void dump_kernel_memory_regions() {
    std::ifstream smaps("/proc/self/smaps");
    if (!smaps.is_open()) {
        std::cerr << "Could not open /proc/self/smaps for deep diagnostics." << std::endl;
        return;
    }

    std::cerr << "\n=== OS MEMORY REGION MAP (Active Regions) ===\n";
    std::string line;
    std::string current_region_header = "";
    
    while (std::getline(smaps, line)) {
        // Headers look like: 555555558000-5555555a9000 rwdp 00000000 00:00 0 [heap]
        if (!line.empty() && std::isxdigit(line[0])) {
            current_region_header = line;
        }
        // If a region has physical pages assigned (RSS), print it if it's significant
        else if (line.find("Rss:") == 0) {
            // Extract the size value from "Rss:         2048 kB"
            size_t rss_val = 0;
            if (sscanf(line.c_str(), "Rss: %lu", &rss_val) == 1 && rss_val > 0) {
                std::cerr << "Region: " << current_region_header << "\n" 
                          << "  Allocated Physical RAM: " << rss_val << " kB\n\n";
            }
        }
    }
}

#include <malloc.h>
void enforce_memory_limit() 
{
    malloc_trim(0);
    static long page_size = sysconf(_SC_PAGESIZE);
    std::ifstream statm("/proc/self/statm");
    if (!statm.is_open()) return;
    
    size_t virtual_pages = 0, resident_pages = 0;
    
    if (statm >> virtual_pages >> resident_pages) {
        size_t current_rss_bytes = resident_pages * page_size;
        
        // 1. Calculate Megabytes (MiB) using double for precision, or integer division if you prefer
        // double current_rss_mb = static_cast<double>(current_rss_bytes) / (1024.0 * 1024.0);

        // 2. Print the usage before the check
        // std::cerr << "=====>>: " << current_rss_mb << " MB" << std::endl; 

        if (current_rss_bytes >= MEMORY_LIMIT_BYTES) {
            std::cerr << "\n[MONITOR] Threshold reached! Halting for GDB attachment..." << std::endl;
            
            // Re-run your smaps printer function here so you have the addresses in your log
            // dump_kernel_memory_regions();
            // Force a trap into GDB
            #if defined(__x86_64__) || defined(__i386__)
                __asm__("int $3"); // Native x86_64 hardware breakpoint
            #else
                std::raise(SIGINT); // Fallback standard signal
            #endif
        }
    }
}
