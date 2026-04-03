// ============================================================
//  ConnectionState.cpp — connStateStr implementation
// ============================================================
#include "Headers/ConnectionState.hpp"

const char* connStateStr(ConnectionState s)
{
    switch (s)
    {
        case CSTATE_READING:    return "READING";
        case CSTATE_PROCESSING: return "PROCESSING";
        case CSTATE_WRITING:    return "WRITING";
        case CSTATE_CLOSING:    return "CLOSING";
        default:                return "UNKNOWN";
    }
}
