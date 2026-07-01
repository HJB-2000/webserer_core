#include "Headers/EventLoop.hpp"
#include "cgi/CgiHandler.hpp"
#include <sstream>
#include <unistd.h>
#include <cstdio>
 
static const size_t CGI_HEADER_BUF_LIMIT = 64 * 1024;
static const size_t CGI_STREAM_HWM       = 256 * 1024;
 
static size_t findHeaderEnd(const Buffer& buf)
{
    const char*  d = buf.data();
    const size_t n = buf.size();
    if (n < 2) return std::string::npos;
    for (size_t i = 0; i + 3 < n; ++i)
        if (d[i]=='\r' && d[i+1]=='\n' && d[i+2]=='\r' && d[i+3]=='\n')
            return i + 4;
    for (size_t i = 0; i + 1 < n; ++i)
        if (d[i]=='\n' && d[i+1]=='\n')
            return i + 2;
    return std::string::npos;
}
 
static int flushCgiHeaders(const char* data, size_t sep,
                           const HttpRequest& req, const ServerConfig& cfg,
                           Buffer& wb)
{
    (void)cfg;
    int         status_code  = 200;
    std::string content_type = "text/html";
    std::string extra;
    size_t line_start = 0;
    for (size_t i = 0; i < sep; ++i)
    {
        if (data[i] != '\n') continue;
        size_t line_len = i - line_start;
        if (line_len > 0 && data[i - 1] == '\r') --line_len;
        if (line_len > 0)
        {
            std::string line(data + line_start, line_len);
            size_t colon = line.find(':');
            if (colon != std::string::npos)
            {
                std::string key = line.substr(0, colon);
                std::string val = line.substr(colon + 1);
                size_t vs = val.find_first_not_of(" \t");
                if (vs != std::string::npos) val = val.substr(vs);
                std::string lkey = key;
                for (size_t j = 0; j < lkey.size(); ++j)
                    lkey[j] = static_cast<char>(std::tolower(static_cast<unsigned char>(lkey[j])));
                if (lkey == "status") { std::istringstream sc(val); sc >> status_code; }
                else if (lkey == "content-type") content_type = val;
                else extra += key + ": " + val + "\r\n";
            }
        }
        line_start = i + 1;
    }
    const char* reason = "OK";
    if      (status_code == 201) reason = "Created";
    else if (status_code == 204) reason = "No Content";
    else if (status_code == 301) reason = "Moved Permanently";
    else if (status_code == 302) reason = "Found";
    else if (status_code == 400) reason = "Bad Request";
    else if (status_code == 403) reason = "Forbidden";
    else if (status_code == 404) reason = "Not Found";
    else if (status_code == 500) reason = "Internal Server Error";
    else if (status_code == 502) reason = "Bad Gateway";
    else if (status_code == 504) reason = "Gateway Timeout";
    std::string conn_val = req.keepAlive() ? "keep-alive" : "close";
    std::ostringstream hdr;
    hdr << "HTTP/1.1 " << status_code << " " << reason << "\r\n"
        << "Server: webserv/1.0\r\n"
        << "Content-Type: " << content_type << "\r\n"
        << "Transfer-Encoding: chunked\r\n"
        << "Connection: " << conn_val << "\r\n"
        << extra << "\r\n";
    const std::string& h = hdr.str();
    wb.append(h.c_str(), h.size());
    return status_code;
}
 
static void writeChunk(Buffer& wb, const char* data, size_t len)
{
    if (len == 0) return;

    std::ostringstream ss;
    ss << std::hex << len << "\r\n";
    
    std::string hexStr = ss.str();

    wb.append(hexStr.c_str(), hexStr.length());
    wb.append(data, len);
    wb.append("\r\n", 2);
}

void EventLoop::_addCgiFd(int result_fd, int client_fd)
{
    try {
        _registerEventFd(result_fd, EV_CGI, EPOLLIN | EPOLLET | EPOLLHUP | EPOLLERR);
        std::cerr << "[EventLoop] CGI fd " << result_fd
                  << " registered for client fd " << client_fd << "\n";
    }
    catch (const std::exception& e) {
        std::cerr << "[EventLoop] failed to register CGI fd " << result_fd
                  << ": " << e.what() << "\n";
        ::close(result_fd);
        throw;
    }
}
 
void EventLoop::_ApiStartCgi(Connection* conn, const CgiRequestInfo& info)
{
    int fds[2];
    if (::pipe(fds) < 0) {
        _responder.sendError(500, *conn->config(), conn->writeBuffer());
        conn->setWriting(); _rearmClient(conn->fd()); return;
    }
    if (::fcntl(fds[0], F_SETFD, FD_CLOEXEC) < 0 ||
        ::fcntl(fds[1], F_SETFD, FD_CLOEXEC) < 0) {
        ::close(fds[0]); ::close(fds[1]);
        _responder.sendError(500, *conn->config(), conn->writeBuffer());
        conn->setWriting(); _rearmClient(conn->fd()); return;
    }
    int result_read_fd  = fds[0];
    int result_write_fd = fds[1];
    if (EventLoop::setNonBlocking(result_read_fd) < 0) {
        ::close(result_read_fd); ::close(result_write_fd);
        _responder.sendError(500, *conn->config(), conn->writeBuffer());
        conn->setWriting(); _rearmClient(conn->fd()); return;
    }
    CgiJob* job = NULL;
    try {
        job = new CgiJob(conn->fd(), result_read_fd,
                         CGI_HEADER_BUF_LIMIT,
                         conn->config()->get_timeout_seconds());
        _addCgiFd(result_read_fd, conn->fd());
        _cgi_jobs[result_read_fd] = job;
    }
    catch (const std::exception& ex) { if(job){delete job;} throw; }

    conn->setCgiRunning();
    _rearmClient(conn->fd());
    int body_fd_to_pass = conn->request().opened_file;
    CgiHandler cgi(conn->request(), *conn->config(), *info.location,
                   info.script_path, conn->get_clientIp());
    if (body_fd_to_pass >= 0)
        cgi.setBodyFd(body_fd_to_pass);
    bool ok = cgi.startCgi(result_write_fd);
    ::close(result_write_fd);
    if (body_fd_to_pass >= 0)
    {
        ::close(body_fd_to_pass);
        conn->request().opened_file = -1;
        conn->request().opened      = false;
    }
    if (!conn->request().tmp_body_path.empty())
    {
        std::remove(conn->request().tmp_body_path.c_str());
        conn->request().tmp_body_path.clear();
    }

    if (ok) {
        job->child_pid = cgi.getChildPid();
    } else {
        _closeCgiJob(result_read_fd);
        _responder.sendError(502, *conn->config(), conn->writeBuffer());
        conn->setWriting();
        _rearmClient(conn->fd());
    }
}

void EventLoop::_handleCgiEvent(int result_fd, uint32_t events)
{
    if (_stopped)
        return;

    std::map<int, CgiJob*>::iterator it = _cgi_jobs.find(result_fd);
    if (it == _cgi_jobs.end())
        return;

    CgiJob*     job  = it->second;
    Connection* conn = _manager->get(job->client_fd);

    if (job->parent_request > LimitInternalRecursion)
    {
        _failCgiJob(result_fd, 502);
        return;
    }
        
    
    if (!conn) {
        _closeCgiJob(result_fd);
        return;
    }
    

    if (events & (EPOLLERR | EPOLLHUP))
    {
        if (events & (EPOLLERR | EPOLLHUP | EPOLLIN))
            std::cerr << " " << std::endl;
        else
            return;
    }

    if (job->headers_sent &&
        conn->writeBuffer().size() >= CGI_STREAM_HWM)
        return;

    char buf[60 * 1024];
    try
    {
            ssize_t n = ::read(result_fd, buf, sizeof(buf));
            if (n > 0)
            {
                job->parent_request++;
                if (!job->headers_sent)
                {
                    job->result_buffer.append_result(buf, static_cast<size_t>(n));
                    size_t body_start = findHeaderEnd(job->result_buffer);
                    if (body_start == std::string::npos)
                    {
                        if (job->result_buffer.size() > FHLS)
                            _failCgiJob(result_fd, 502);
                        return;
                    }
                    flushCgiHeaders(job->result_buffer.data(), body_start,
                                    conn->request(), *conn->config(),
                                    conn->writeBuffer());
                    job->headers_sent = true;
                    size_t leftover = job->result_buffer.size() - body_start;
                    writeChunk(conn->writeBuffer(),
                               job->result_buffer.data() + body_start,
                               leftover);
                    
                    job->body_written += leftover;
                    job->result_buffer.earase();
                    conn->setWriting();
                    _rearmClient(conn->fd());
                    if (conn->writeBuffer().size() >= CGI_STREAM_HWM)
                        return;
                }
                else
                {
                    writeChunk(conn->writeBuffer(), buf,
                                static_cast<size_t>(n));
                    job->body_written += static_cast<size_t>(n);
                    conn->setWriting();
                    _rearmClient(conn->fd());

                    if (conn->writeBuffer().size() >= CGI_STREAM_HWM)
                        return;
                }
                return ;
            }
            if (n == 0)
            {
                job->parent_request = 0;
                _finishCgiJob(result_fd);
                return;
            }

            if (n < 0 || _stopped)
                return;
        }
    catch (const BodyLimitException&)
    {
        if (_stopped)
            return;
        _failCgiJob(result_fd, 413);
    }
}