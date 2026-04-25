#ifndef CGI_JOB_HPP
#define CGI_JOB_HPP
#include <ctime>
#include "buffer.hpp"


struct CgiJob
{
    int         client_fd;
    int         result_fd;
    Buffer      result_buffer;
    time_t      start_time;

    CgiJob(int cfd, int rfd, size_t max_size)
    :       client_fd(cfd)
    ,       result_fd(rfd)
    ,       result_buffer(max_size)
    ,       start_time(std::time(NULL)) 
    {}
};


#endif