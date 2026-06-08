#include "Headers/ResponseHandler.hpp"
#include <sstream>
void ResponseHandler::handleCgiOutput(
    const HttpRequest&  req,
    const ServerConfig& cfg,
    const Buffer&       cgi_output,
    Buffer&             wb)
{
    const char*  data = cgi_output.data();
    const size_t size = cgi_output.size();

    size_t sep       = std::string::npos;
    size_t body_skip = 0;

    for (size_t i = 0; i + 3 < size; ++i)
    {
        if (data[i]=='\r' && data[i+1]=='\n' && data[i+2]=='\r' && data[i+3]=='\n')
        {
            sep = i;
            body_skip = 4;
            break;
        }
    }

    if (sep == std::string::npos)
    {
        for (size_t i = 0; i + 1 < size; ++i)
        {
            if (data[i]=='\n' && data[i+1]=='\n')
            {
                sep = i;
                body_skip = 2;
                break;
            }
        }
    }

    if (sep == std::string::npos)
    {
        _sendErrorInternal(502, req, cfg, wb);
        return;
    }

    int         status_code   = 200;
    std::string content_type  = "text/html";
    std::string extra_headers;

    size_t line_start = 0;
    for (size_t i = 0; i < sep; ++i)
    {
        if (data[i] == '\n')
        {
            size_t line_len = i - line_start;
            if (line_len > 0 && data[i - 1] == '\r')
                --line_len;

            if (line_len > 0)
            {
                std::string line(data + line_start, line_len);
                
                size_t colon = line.find(':');
                if (colon != std::string::npos)
                {
                    std::string key = line.substr(0, colon);
                    std::string val = line.substr(colon + 1);

                    size_t vs = val.find_first_not_of(" \t");
                    if (vs != std::string::npos)
                        val = val.substr(vs);

                    std::string lkey = key;
                    for (size_t j = 0; j < lkey.size(); ++j)
                        lkey[j] = static_cast<char>(
                            std::tolower(static_cast<unsigned char>(lkey[j])));

                    if (lkey == "status")
                    {
                        std::istringstream sc(val);
                        sc >> status_code;
                    }
                    else if (lkey == "content-type")
                    {
                        content_type = val;
                    }
                    else
                    {
                        extra_headers += key + ": " + val + "\r\n";
                    }
                }
            }
            line_start = i + 1;
        }
    }

    size_t body_size = size - (sep + body_skip);
    
    _writeHeaders(status_code, content_type, body_size, extra_headers, req, wb);
    
    if (req.method != "HEAD" && body_size > 0)
    {
        wb.append(data + sep + body_skip, body_size);
    }
}
