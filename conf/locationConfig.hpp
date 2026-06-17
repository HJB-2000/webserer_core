#ifndef LOCATIONCONFIG_HPP
#define LOCATIONCONFIG_HPP

class Server;

#include <string>
#include <map>
#include <vector>


class Location
{
    public :
        Location();
        Location(const Location& obj);
        Location(const Server& obj_server);
        Location& operator=(const Location& obj);
        ~Location();

        void setPath(std::string path);
        void setMethods(std::string method);
        void setRoot(const std::string& root);
        void setCGI_extensions(const std::vector<std::string>& extensions);
        void setCGI_paths(const std::vector<std::string>& paths);
        void setIndex_s(const std::string& index_s);
        void setAutoindex(const std::string& autoindex);
        void setUploadStore(const std::string& upload);
        void setClientMaxBodySize(long long size);
        void setReturnRedirection(int code, std::string path);
        void set_error_page_loc(int err_code, std::string err_path);

        std::string getPath() const;
        std::vector<std::string> getMethods() const;
        std::string getRoot() const;
        std::vector<std::string> getIndex_s() const;
        bool getAutoindex() const;
        const std::map<std::string, std::string>& getCGI_map() const;
        bool hasPartialCGIConfig() const;
        std::string getUploadStore() const;
        size_t getClientMaxBodySize() const;
        int getReturnRedirection_code() const;
        std::string getReturnRedirection_path() const;
        std::map<int, std::string> get_error_page_loc() const;
        bool getRedirectEnabled() const;
        void clear_index();

        void check_for_allowed_methods();
        void set_default_conf(int num);
    private:
        std::string _path;
        std::string _root;
        std::vector<std::string> _index_Files;
        bool _autoindex;
        std::vector<std::string> _allowed_methods;
        std::vector<std::string> _cgi_paths;
        std::vector<std::string> _cgi_extensions;
        std::map<std::string, std::string> _cgi_map;
        std::string _upload;
        int _return_code;
        std::string _return_value;
        long long _client_max_body_size;
        std::map<int, std::string> _error_page;
        bool _redirect_enabled;
};

#endif