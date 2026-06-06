#include "Headers/EventLoop.hpp"
#include "serverConfig.hpp"
#include "Headers/API_conf.hpp"
#include "Headers/Logger.hpp"

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
        loop.run();
        // Note: stop() already closes all server fds, so don't close them here
        // Otherwise valgrind reports "fd already closed" errors
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
