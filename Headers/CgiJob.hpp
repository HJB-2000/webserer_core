#ifndef CGI_JOB_HPP
#define CGI_JOB_HPP
#include <ctime>
#include <cstddef>
#include <string>
#include <sys/types.h>
#include "buffer.hpp"


struct CgiJob
{
    int         client_fd;
    int         result_fd;
    pid_t       child_pid;
    Buffer      result_buffer;
    time_t      start_time;
    int         _timeout_seconds; // to check based on config file
    // Non-blocking stdin write state (only used when the request has a body)
    int         stdin_fd;
    const std::string* stdin_body;;
    size_t      stdin_offset;

    CgiJob(int cfd, int rfd, size_t max_size, int timeout)
    :       client_fd(cfd)
    ,       result_fd(rfd)
    ,       child_pid(-1)
    ,       result_buffer(max_size)
    ,       start_time(std::time(NULL))
    ,       _timeout_seconds(timeout)
    ,       stdin_fd(-1)
    ,       stdin_body()
    ,       stdin_offset(0)
    {}
};


#endif