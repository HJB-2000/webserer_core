// Non-blocking server core (C++98)
// This header defines the minimal networking core for:
// - creating one or more listening sockets
// - registering it in epoll
// - accepting many clients in edge-trigger mode
// - reading client traffic without blocking
#ifndef WEBSERVER_HPP
#define WEBSERVER_HPP
#include <iostream>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <cstring>
#include <cerrno>
#include <vector>
#include <map>
#include <string>
#include <ctime>

/*lib to use memset*/
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <fcntl.h>
/**************** */
#define BACKLOG 10
#define MAX_EVENTS 64
#define READ_BUFFER_SIZE 4096
#define MAX_CLIENT_BUFFER_SIZE (1024 * 1024)
#define EPOLL_WAIT_TIMEOUT_MS 100
#define CLIENT_IDLE_TIMEOUT_SEC 30



namespace socket_connection {
    // socket_ owns the listener socket, epoll instance and tracked client fds.
    // It exposes a small API so main() can:
    //   1) create_socket() once
    //   2) call accept_connection() repeatedly as an event pump
    class socket_
    {
        public :
            enum ConnectionState {
                CS_READING = 0,
                CS_PROCESSING,
                CS_WRITING,
                CS_CLOSING
            };

            struct Connection {
                int fd;
                ConnectionState state;
                std::string read_buffer;
                std::string write_buffer;
                size_t write_offset;
                std::time_t last_active;
                bool keep_alive;

                Connection(): fd(-1), state(CS_READING), write_buffer(), write_offset(0),
                    last_active(0), keep_alive(true) {}
            };

            // Core API: expose the internal connection object created by accept().
            // Returned pointer is borrowed (owned by socket_); do not delete it.
            Connection* get_connection_object(int fd)
            {
                std::map<int, Connection>::iterator it = this->clients.find(fd);
                if (it == this->clients.end())
                    return NULL;
                return &(it->second);
            }

            const Connection* get_connection_object(int fd) const
            {
                std::map<int, Connection>::const_iterator it = this->clients.find(fd);
                if (it == this->clients.end())
                    return NULL;
                return &(it->second);
            }

            size_t connection_count() const
            {
                return this->clients.size();
            }

        private :
            /*important data*/
            const char *node;
            const char *service;
            struct addrinfo hints;
            struct addrinfo *res;
            struct addrinfo *p;
            int getaddrinfo_result;
            int already;
            int epoll_fd;
            std::map<int, Connection> clients;
            std::vector<int> listener_fds;
            /*make the obeject unique*/
            socket_(const socket_ &);
            socket_ &operator=(const socket_ &);
            /************/
            
            protected :
            int socket_fd;
            char Hostname[HOST_NAME_MAX];
            void set_hints();
            void set_addrinfo_();
            int get_addrinfo_();
            void setup();

            // Utility: set a fd to O_NONBLOCK.
            int set_non_blocking(int fd);

            // Epoll registration helpers for ADD/MOD/DEL operations.
            int add_fd_to_epoll(int fd, uint32_t events);
            int mod_fd_in_epoll(int fd, uint32_t events);
            int remove_fd_from_epoll(int fd);
            int rearm_client_events(int fd);

            // Edge-trigger helpers:
            // - accept_all_pending(): drain all waiting incoming clients.
            // - read_from_client(): drain all readable bytes until EAGAIN.
            int accept_all_pending(int listener_fd);
            int read_from_client(int fd);
            int write_to_client(int fd);

            // Integration entry point:
            // When HttpParser is introduced, this method becomes the handoff
            // from raw read_buffer bytes to parsed request fields.
            void process_client_buffer(int fd);

            // Integration entry point:
            // Replace simple response queueing with ResponseHandler output.
            void queue_simple_response(int fd, const std::string& status,
                const std::string& body, bool keep_alive);

            // Core-only helpers used until dedicated parser module exists.
            bool request_complete(const std::string& data) const;
            bool should_keep_alive(const std::string& request) const;
            void sweep_idle_clients();

            // Close path for a single client: epoll DEL + close + erase from tracking.
            void close_client(int fd);

            // Safety check to ignore stale/untracked fds from epoll events.
            bool is_client_fd(int fd) const;
            bool is_listener_fd(int fd) const;

        public :
            socket_();
            ~socket_();

            // Build listener + epoll context.
            // Returns 0 on success, -1 on failure.
            int create_socket();

            // Execute one epoll cycle and dispatch events.
            // Returns 0 on success, -1 on fatal error.
            int accept_connection();




    };

};







#endif