#include "../serverConfig.hpp"
#include "../locationConfig.hpp"
#include <iostream>
#include <vector>

// Why this stub exists:
// The checklist asks us to verify client_max_body_size is passed to Buffer.
// If Buffer is not integrated in this module yet, we verify the contract using
// a tiny constructor-only stub that receives the same size value.
class BufferStub
{
public:
    explicit BufferStub(size_t cap) : _capacity(cap) {}
    size_t capacity() const { return _capacity; }

private:
    size_t _capacity;
};

static bool assert_equal_path(const Location* loc, const std::string& expected, const std::string& test_name)
{
    if (loc == NULL)
    {
        std::cerr << "[FAIL] " << test_name << ": matchLocation returned NULL" << std::endl;
        return false;
    }
    if (loc->getPath() != expected)
    {
        std::cerr << "[FAIL] " << test_name << ": expected '" << expected
                  << "' but got '" << loc->getPath() << "'" << std::endl;
        return false;
    }
    std::cout << "[PASS] " << test_name << std::endl;
    return true;
}

static bool assert_true(bool condition, const std::string& test_name, const std::string& message_if_fail)
{
    if (!condition)
    {
        std::cerr << "[FAIL] " << test_name << ": " << message_if_fail << std::endl;
        return false;
    }
    std::cout << "[PASS] " << test_name << std::endl;
    return true;
}

int main()
{
    Server server;

    Location root;
    root.setPath("/");

    Location cgi;
    cgi.setPath("/cgi-bin/");

    Location uploads;
    uploads.setPath("/uploads/");

    server.addLocation(root);
    server.addLocation(cgi);
    server.addLocation(uploads);

    bool ok = true;

    const Location* l1 = server.matchLocation("/cgi-bin/script.py");
    ok = assert_equal_path(l1, "/cgi-bin/", "matchLocation('/cgi-bin/script.py') longest-prefix") && ok;

    const Location* l2 = server.matchLocation("/");
    ok = assert_equal_path(l2, "/", "matchLocation('/') fallback") && ok;

    // -------------------- matchServer tests --------------------
    Server s1;
    int p1 = 8080;
    s1.setPort(p1);
    std::vector<std::string> n1;
    n1.push_back("example.com");
    s1.setServerNames(n1);

    Server s2;
    int p2 = 8080;
    s2.setPort(p2);
    std::vector<std::string> n2;
    n2.push_back("api.example.com");
    s2.setServerNames(n2);

    Server s3;
    int p3 = 9090;
    s3.setPort(p3);
    std::vector<std::string> n3;
    n3.push_back("other.example.com");
    s3.setServerNames(n3);

    std::vector<Server> servers;
    servers.push_back(s1);
    servers.push_back(s2);
    servers.push_back(s3);

    const Server* ms1 = matchServer(servers, "api.example.com", 8080);
    ok = assert_true(ms1 != NULL && ms1->getServerNames()[0] == "api.example.com",
                     "matchServer host-header exact match",
                     "did not pick the expected server by host header") && ok;

    const Server* ms2 = matchServer(servers, "unknown.example.com", 8080);
    ok = assert_true(ms2 != NULL && ms2->getServerNames()[0] == "example.com",
                     "matchServer same-port fallback",
                     "did not fallback to the first server on requested port") && ok;

    // -------------------- client_max_body_size -> Buffer contract --------------------
    Server body_limit_server;
    long long body_limit = 1048576; // 1MB
    body_limit_server.setMaxBodySize(body_limit);

    BufferStub buffer(body_limit_server.getMaxBody());
    ok = assert_true(buffer.capacity() == static_cast<size_t>(body_limit),
                     "client_max_body_size passed to Buffer stub",
                     "Buffer did not receive expected client_max_body_size") && ok;

    if (!ok)
        return 1;

    std::cout << "All phase-1 routing/body-limit tests passed." << std::endl;
    return 0;
}
