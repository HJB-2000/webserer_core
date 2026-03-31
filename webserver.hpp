#pragma once
#ifndef WEBSERVER_HPP
#define WEBSERVER_HPP
#include <iostream>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <cstring>
#include <cerrno>

/*lib to use memset*/
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <stdbool.h>
/**************** */
#define BACKLOG 10



namespace socket_connection {
    class socket_
    {
        private :
            /*important data*/
            const char *node;
            const char *service;
            struct addrinfo hints;
            struct addrinfo *res;
            struct addrinfo *p;
            struct sockaddr_storage their_addr;
            socklen_t addr_size;
            char ipstr[INET6_ADDRSTRLEN];
            int addrinfo_;
            int already;
            /*make the obeject unique*/
            socket_(const socket_ &obj){};
            socket_ &operator=(const socket_ &obj){return *this;};
            /************/
            
            protected :
            int socket_fd;
            int client_socket_fd;
            char Hostname[HOST_NAME_MAX];
            void set_hints();
            void set_addrinfo_();
            int get_addrinfo_();
            void setup();
            int who_are_you();
            void *get_addrptr(struct sockaddr * sa);

        public :
            socket_();
            ~socket_();
            int create_socket();
            int accept_connection();




    };

};







#endif