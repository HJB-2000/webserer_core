#include "parserConf.hpp"
#include "serverConfig.hpp"
#include <string>
#include <iostream>
#include <map>

using namespace std;

void ParserConf::printer_of_conf_parser()
{
    cout << "========== Parsed Config ==========" << endl;

    // // events
    // cout << "\n[events]" << endl;
    // cout << "  worker_connections = " << _events.get_worker_connections() << endl;
    // cout << "  event_model = " << _events.get_event_model() << endl;

    // http
    cout << "\n[http]" << endl;
    cout << "  client_max_body_size = " << _http.get_cl_mx_bd_sz() << endl;

    map<int, string> http_errs = _http.get_error_page();
    for(map<int,string>::iterator it = http_errs.begin(); it != http_errs.end(); ++it)
    {
        cout << "  error_page: " << it->first << " -> " << it->second << endl; 
    }
    // servers
    const vector<Server>& servers = _http.get_all_servers();

    // Demo: show routing behavior for matchServer + matchLocation
    if (!servers.empty())
    {
        const int demo_port = servers[0].getPort();
        string demo_host = "";
        vector<string> first_names = servers[0].getServerNames();
        if (!first_names.empty())
            demo_host = first_names[0];

        const Server* matched_server = matchServer(servers, demo_host, demo_port);
        cout << "\n[routing demo]" << endl;
        cout << "  matchServer(host='" << demo_host << "', port=" << demo_port << ") -> ";
        if (matched_server != NULL)
            cout << "matched server on port " << matched_server->getPort() << endl;
        else
            cout << "no match" << endl;

        if (matched_server != NULL)
        {
            const Location* matched_location = matched_server->matchLocation("/cgi-bin/script.py");
            cout << "  matchLocation('/cgi-bin/script.py') -> ";
            if (matched_location != NULL)
                cout << matched_location->getPath() << endl;
            else
                cout << "no location" << endl;

            const Location* fallback_location = matched_server->matchLocation("/");
            cout << "  matchLocation('/') -> ";
            if (fallback_location != NULL)
                cout << fallback_location->getPath() << endl;
            else
                cout << "no location" << endl;
        }

        const Server* fallback_server = matchServer(servers, "unknown-host.test", demo_port);
        cout << "  matchServer(host='unknown-host.test', port=" << demo_port << ") -> ";
        if (fallback_server != NULL)
            cout << "fallback server on port " << fallback_server->getPort() << endl;
        else
            cout << "no match" << endl;
    }

    for(size_t i = 0; i < servers.size(); i++)
    {
        cout << "\n  --- Server #" << i << " ---" << endl;
        cout << "    host = " << servers[i].getHost() << endl; 
        cout << "    port = " << servers[i].getPort() << endl; 
        cout << "    root = " << servers[i].getRoot() << endl;
        vector<string> server_index_s = servers[i].getIndex_s();
        for (size_t si = 0; si < server_index_s.size(); ++si) {
            cout << "    index = [" << server_index_s[si] << "]" << endl;
        }
        cout << "    client_max_body_size = " << servers[i].getMaxBody() << endl;
        cout << "    timeout = " << servers[i].get_timeout_seconds() << endl;
        
        vector<string> name_servers = servers[i].getServerNames();
        for(size_t j = 0; j < name_servers.size(); j++)
        {
            cout << "    server_name = " << name_servers[j] << endl; 
        }
        
        map<int, string> errpage = servers[i].getErrorPageMap();
        for(map<int,string>::iterator it = errpage.begin(); it != errpage.end(); ++it)
        {
            cout << "    error_page: " << it->first << " -> " << it->second << endl; 
        }
        // locations
        vector<Location> location_in_server = servers[i].get_locations();
        for(size_t k = 0; k < location_in_server.size(); k++)
        {
            cout << "    --- Location #" << k << " ---" << endl;
            cout << "      path = " << location_in_server[k].getPath() << endl;
            cout << "      root = " << location_in_server[k].getRoot() << endl;
            vector<string> loc_index_s = location_in_server[k].getIndex_s();
            for (size_t li = 0; li < loc_index_s.size(); ++li) {
                cout << "      index = [" << loc_index_s[li] << "]" << endl;
            }
            cout << "      autoindex = " << location_in_server[k].getAutoindex() << endl;
            map<int, string> location_err_page = location_in_server[k].get_error_page_loc();
            for(map<int,string>::iterator it = location_err_page.begin(); it != location_err_page.end(); ++it)
            {
                cout << "      error_page: " << it->first << " -> " << it->second << endl; 
            }
            if(!location_in_server[k].getCGI_extensions().empty())
            {
                cout << "      cgi_ext = ";
                const vector<string>& exts = location_in_server[k].getCGI_extensions();
                for (size_t ei = 0; ei < exts.size(); ++ei)
                {
                    if (ei > 0) cout << " ";
                    cout << exts[ei];
                }
                cout << endl;
            }
            if(!location_in_server[k].getCGI_path().empty())
                cout << "      cgi_path = " << location_in_server[k].getCGI_path() << endl;
            if(!location_in_server[k].getUploadStore().empty())
                cout << "      upload_store = " << location_in_server[k].getUploadStore() << endl;
            // if(location_in_server[k].getClientmax_body_size() > 0)
            //     cout << "      client_max_body_size = " << location_in_server[k].getClientmax_body_size() << endl;
            if(location_in_server[k].getReturnRedirection_code() > 0)
                cout << "      rediection return code = " << location_in_server[k].getReturnRedirection_code() << endl;
            if(!location_in_server[k].getReturnRedirection_path().empty())
                cout << "      redirection return path = " << location_in_server[k].getReturnRedirection_path() << endl;
            // Print redirect enabled flag
            cout << "      redirect_enabled = " << (location_in_server[k].getRedirectEnabled() ? "true" : "false") << endl;
            
            vector<string> methods = location_in_server[k].getMethods();
            for(size_t m = 0; m < methods.size(); m++)
            {
                cout << "      allowed_methods = " << methods[m] << endl; 
            }
        }
    }
    cout << "\n===================================" << endl;
}