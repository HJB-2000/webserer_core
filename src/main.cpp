#include "Headers/EventLoop.hpp"
#include "serverConfig.hpp"
#include "Headers/API_conf.hpp"
#include "Headers/Logger.hpp"

#include <iostream>
#include <fstream>
#include <unistd.h>
#include <cstdlib>

// Hard limit: 2 GB = 2 * 1024 * 1024 * 1024 bytes
const size_t MEMORY_LIMIT_BYTES = 2ULL * 1024 * 1024 * 1024;

// Helper to dump the OS-level memory mappings
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

void enforce_memory_limit() {
    static long page_size = sysconf(_SC_PAGESIZE);
    std::ifstream statm("/proc/self/statm");
    if (!statm.is_open()) return;

    size_t virtual_pages = 0, resident_pages = 0;
    if (statm >> virtual_pages >> resident_pages) {
        size_t current_rss_bytes = resident_pages * page_size;

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

static EventLoop* g_loop = NULL;
static void sig_handler(int)
{
    if (g_loop)
        g_loop->stop();
}

int main(int argc, char* argv[])
{
    Logger::instance().open("webserv.log");
    std::vector<ServerConfig> servers;
    try {    
        servers = API_conf(argc, argv);
    }
    catch (const std::runtime_error& e)
    {
        std::cerr << "|" << e.what() << "|" << "\n";
        return 1;
    }
        
    try {
        EventLoop            loop;
        std::vector<int>     listen_fds;

        for (size_t i = 0; i < servers.size(); ++i)
        {
            int fd = make_listener(servers[i].getHost().c_str(), servers[i].getPort());
            if (fd < 0)
            {
                std::cerr << "[core] fatal: could not create listener\n";
                return 1;
            }
            listen_fds.push_back(fd);
            try { 
                loop.addServerSocket(fd, &servers[i]);
            }
            catch(const std::exception& ex) { 
                std::cerr << "[core] fatal: failed to register server socket: " << ex.what() << "\n";  
                ::close(fd);  
                listen_fds.pop_back();  
                Logger::instance().close();  
                return 1;  
            }
        }

        g_loop = &loop;
        std::signal(SIGINT,  sig_handler);
        std::signal(SIGTERM, sig_handler);
        std::signal(SIGPIPE, SIG_IGN);
        std::cerr << "[core] server ready — press Ctrl+C to stop\n";
        enforce_memory_limit();
        loop.run();
        for (size_t i = 0; i < listen_fds.size(); ++i)
            ::close(listen_fds[i]);
        std::cerr << "[core] shutdown complete\n";
        Logger::instance().close();
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[core] fatal error: " << ex.what() << "\n";  
        Logger::instance().close();  
        return 1;
    }
}
