#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include <sys/epoll.h>
#include <sys/socket.h>  
#include <unistd.h>      
#include <ctime>         
#include <cerrno>        

#include "buffer.hpp"
#include "HttpRequest.hpp"
#include "ConnectionState.hpp"

#include "serverConfig.hpp" 
class HttpParser;       
class ResponseHandler;  

class Connection
{
public:

    Connection(int fd, const ServerConfig* config);
    ~Connection();

    ssize_t recv();
    ssize_t send();

    void reset();

    epoll_event buildEpollEvent();

    bool isTimedOut(time_t timeout_seconds) const;

    int                  fd()          const;
    ConnectionState      state()       const;
    time_t               lastActive()  const;
    const ServerConfig*  config()      const;
    
    Buffer&       readBuffer();
    const Buffer& readBuffer()  const;
    Buffer&       writeBuffer();
    const Buffer& writeBuffer() const;
    
    HttpRequest&       request();
    const HttpRequest& request() const;
    
    void setProcessing();
    void setWriting();
    void setClosing();
    void setReading();
    void setCgiRunning();

    void setPeerHalfClosed();
    bool peerHalfClosed() const;
    void updateBufferSizes(size_t new_max);


    const std::string& get_clientIp() const;
    void setClientIp(const std::string& ip);
private:

    Connection(const Connection&);
    Connection& operator=(const Connection&);

    void _touchActive();

    int                 _fd;
    const ServerConfig* _config;
    Buffer              _read_buffer;
    Buffer              _write_buffer;
    HttpRequest         _request;
    ConnectionState     _state;
    time_t              _last_active;
    bool                _peer_half_closed;
    std::string         _client_ip;

};

#endif
