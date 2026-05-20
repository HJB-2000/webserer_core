# BUG 5 — `addrToString` Does Unconditional IPv4 Cast Without Address-Family Check

## Location
`src/ConnectionManager.cpp` — `addrToString(const struct sockaddr_storage& addr)`

## Root Cause

```cpp
static std::string addrToString(const struct sockaddr_storage& addr)
{
    // No check of addr.ss_family — assumes AF_INET always
    const struct sockaddr_in* sin =
        reinterpret_cast<const struct sockaddr_in*>(&addr);

    char buf[INET_ADDRSTRLEN];
    ::inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf));
    return std::string(buf);
}
```

When a Linux system has IPv6 enabled and the server binds `0.0.0.0` (AF_INET),
the kernel may still deliver connections as IPv6-mapped IPv4 addresses
(`::ffff:1.2.3.4`) depending on the socket options and OS configuration.

In that case `addr.ss_family == AF_INET6`, but the cast to `sockaddr_in` reads
the wrong bytes as `sin_addr` → `inet_ntop` prints garbage → `REMOTE_ADDR`
CGI variable is wrong → CGI scripts that trust REMOTE_ADDR are broken.

Additionally, `inet_ntop(AF_INET, ...)` on an `AF_INET6` sockaddr reads 4 bytes
starting at offset 4 of the `sockaddr_in6` structure, which is the flow-info
field — not the address. This is well-defined memory access but logically wrong.

## How to Trigger

### Method 1 — Enable IPv6 dual-stack via sysctl (requires Linux with IPv6)

```bash
# Check if IPv6 is available
cat /proc/net/if_inet6

# Connect via IPv6 loopback (::1)
python3 trigger.py --ipv6

# If the server is bound to 0.0.0.0 with IPV6_V6ONLY=0 (default on Linux),
# IPv6 connections are accepted and addr.ss_family will be AF_INET6.
```

### Method 2 — Read the current REMOTE_ADDR from CGI output

`trigger.py` compares:
- The `REMOTE_ADDR` reported by the CGI script
- The expected address based on the connection's source IP

If they differ, the bug is confirmed.

### Method 3 — Direct sockaddr_storage inspection (code audit)

Apply the provided patch to add an assertion:
```cpp
assert(addr.ss_family == AF_INET);  // will fire on IPv6 connection
```

## Expected Result (bug present)
CGI script reports a wrong or garbled `REMOTE_ADDR`
(e.g. `"16.0.64.0"` instead of `"127.0.0.1"`).

## Expected Result (bug fixed)
`REMOTE_ADDR` correctly shows the client's IPv4 address.

## Fix

```cpp
static std::string addrToString(const struct sockaddr_storage& addr)
{
    if (addr.ss_family == AF_INET6)
    {
        const struct sockaddr_in6* sin6 =
            reinterpret_cast<const struct sockaddr_in6*>(&addr);
        // Check for IPv4-mapped IPv6 address (::ffff:x.x.x.x)
        const uint8_t* b = sin6->sin6_addr.s6_addr;
        bool mapped = (b[0]==0 && b[1]==0 && b[2]==0  && b[3]==0  &&
                       b[4]==0 && b[5]==0 && b[6]==0  && b[7]==0  &&
                       b[8]==0 && b[9]==0 && b[10]==0xff && b[11]==0xff);
        if (mapped) {
            // Re-interpret as IPv4
            char buf[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, b + 12, buf, sizeof(buf));
            return std::string(buf);
        }
        char buf[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &sin6->sin6_addr, buf, sizeof(buf));
        return std::string(buf);
    }
    const struct sockaddr_in* sin =
        reinterpret_cast<const struct sockaddr_in*>(&addr);
    char buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf));
    return std::string(buf);
}
```
