#ifndef CGI_JOB_HPP
#define CGI_JOB_HPP
#include <ctime>
#include <sys/types.h>
#include "buffer.hpp"


struct CgiJob
{
    int         client_fd;
    int         result_fd;
    pid_t       child_pid;
    Buffer      result_buffer;
    time_t      start_time;

    CgiJob(int cfd, int rfd, size_t max_size)
    :       client_fd(cfd)
    ,       result_fd(rfd)
    ,       child_pid(-1)
    ,       result_buffer(max_size)
    ,       start_time(std::time(NULL))
    {}
};


#endif