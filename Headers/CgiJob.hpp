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
    int         _timeout_seconds;
    int         stdin_fd;

    bool        headers_sent;
    size_t      body_written;


    CgiJob(int cfd, int rfd, size_t max_size, int timeout)
    :       client_fd(cfd)
    ,       result_fd(rfd)
    ,       child_pid(-1)
    ,       result_buffer(max_size)
    ,       start_time(std::time(NULL))
    ,       _timeout_seconds(timeout)
    ,       stdin_fd(-1)
    ,       headers_sent(false)
    ,       body_written(0)
    {}
};


#endif