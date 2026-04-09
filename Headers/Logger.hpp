// ============================================================
//  Logger.hpp  —  log file handler + performance / state logging
//
//  Strategy: TeeStreambuf intercepts std::cerr and mirrors
//  every byte to both the original terminal AND a log file.
//
//  Every line written to the log file is prefixed with an
//  ISO-8601 UTC timestamp:
//    [2024-11-04 12:00:00] [EventLoop] starting
//
//  Extra tagged channels (write directly to the log file,
//  NOT to the terminal — keep dev output clean):
//    Logger::instance().perf("accept 12 conns in 3 ms")
//    Logger::instance().state("fd=5 READING->PROCESSING")
//    Logger::instance().log(Logger::WARN, "disk full")
//
//  Stopwatch helper for inline timing:
//    Logger::Stopwatch sw;
//    // ... work ...
//    Logger::instance().perf("parse " + sw.str());
//
//  Usage in main():
//    Logger::instance().open("webserv.log");  // at startup
//    Logger::instance().setLevel(Logger::INFO); // optional filter
//    ...
//    Logger::instance().close();              // before return
//
//  After open(), ALL std::cerr output is automatically captured.
//  C++98 compliant.  No external dependencies.
// ============================================================
#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <streambuf>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <ctime>
#include <sys/time.h>   // gettimeofday


// ────────────────────────────────────────────────────────────
//  TeeStreambuf
//
//  Sits between std::cerr and its original destination.
//  Each character is forwarded to:
//    1. _orig  — the original stderr streambuf (terminal)
//    2. _file  — the open log file (with UTC timestamp)
// ────────────────────────────────────────────────────────────
class TeeStreambuf : public std::streambuf
{
public:

    TeeStreambuf(std::streambuf* orig, std::ofstream& file)
        : _orig(orig)
        , _file(file)
        , _at_line_start(true)
    {}

protected:

    virtual int overflow(int c)
    {
        if (c == EOF)
            return c;

        const char ch = static_cast<char>(c);

        // 1. Mirror to terminal
        _orig->sputc(ch);

        // 2. Write to log file with timestamp on each new line
        if (_at_line_start)
        {
            const std::string ts = _utcTimestamp();
            _file.write(ts.c_str(), static_cast<std::streamsize>(ts.size()));
            _at_line_start = false;
        }
        _file.put(ch);

        if (c == '\n')
        {
            _at_line_start = true;
            _file.flush();
        }

        return c;
    }

    virtual std::streamsize xsputn(const char* s, std::streamsize n)
    {
        for (std::streamsize i = 0; i < n; ++i)
            overflow(static_cast<unsigned char>(s[i]));
        return n;
    }

private:

    static std::string _utcTimestamp()
    {
        time_t     now = ::time(NULL);
        struct tm* gmt = ::gmtime(&now);
        char       buf[24];
        ::strftime(buf, sizeof(buf), "[%Y-%m-%d %H:%M:%S] ", gmt);
        return std::string(buf);
    }

    std::streambuf* _orig;
    std::ofstream&  _file;
    bool            _at_line_start;
};


// ────────────────────────────────────────────────────────────
//  Logger  —  singleton
// ────────────────────────────────────────────────────────────
class Logger
{
public:

    // ── Log levels ────────────────────────────────────────────
    enum Level { DEBUG = 0, INFO, WARN, ERROR, PERF, STATE };

    static Logger& instance()
    {
        static Logger inst;
        return inst;
    }

    // ── Lifecycle ─────────────────────────────────────────────

    void open(const std::string& path)
    {
        _path = path;
        _file.open(path.c_str(), std::ios::app);
        if (!_file.is_open())
        {
            std::cerr << "[Logger] WARNING: cannot open log file '"
                      << path << "' — logging to stderr only\n";
            return;
        }

        _orig_cerr = std::cerr.rdbuf();
        _tee       = new TeeStreambuf(_orig_cerr, _file);
        std::cerr.rdbuf(_tee);

        std::cerr << "========================================"
                     "========================================\n"
                  << "[Logger] log session started  —  file: " << path << "\n"
                  << "========================================"
                     "========================================\n";
    }

    void close()
    {
        if (!_tee)
            return;

        std::cerr << "========================================"
                     "========================================\n"
                  << "[Logger] log session ended\n"
                  << "========================================"
                     "========================================\n\n";

        std::cerr.rdbuf(_orig_cerr);
        delete _tee;
        _tee       = NULL;
        _orig_cerr = NULL;
        _file.close();
    }

    // ── Level filter ──────────────────────────────────────────
    void  setLevel(Level min) { _min_level = min; }
    Level getLevel() const    { return _min_level; }

    // ── Tagged log channels ───────────────────────────────────
    //
    // log()   — general: writes to log file + terminal via cerr
    // perf()  — performance data: log file only (no terminal noise)
    // state() — connection/server state transitions: log file only

    void log(Level level, const std::string& msg)
    {
        if (level < _min_level)
            return;
        // Route through cerr so TeeStreambuf timestamps it.
        std::cerr << "[" << _levelTag(level) << "] " << msg << "\n";
    }

    // Performance: written directly to the log file only.
    // Use for timing data, throughput metrics, etc.
    void perf(const std::string& msg)
    {
        _writeTagged("PERF ", msg);
    }

    // State transitions: written directly to the log file only.
    // Use for connection FSM transitions, accept/close events, etc.
    void state(const std::string& msg)
    {
        _writeTagged("STATE", msg);
    }

    const std::string& path() const { return _path; }

    // ── Stopwatch ─────────────────────────────────────────────
    //
    // Measures elapsed wall-clock time in milliseconds.
    //
    //   Logger::Stopwatch sw;
    //   // ... work ...
    //   Logger::instance().perf("request processed in " + sw.str());
    //
    class Stopwatch
    {
    public:
        Stopwatch() { ::gettimeofday(&_start, NULL); }

        // Elapsed milliseconds since construction.
        double elapsed_ms() const
        {
            struct timeval now;
            ::gettimeofday(&now, NULL);
            return (now.tv_sec  - _start.tv_sec)  * 1000.0
                 + (now.tv_usec - _start.tv_usec) / 1000.0;
        }

        // Returns "X.XXX ms" ready for embedding in a log message.
        std::string str() const
        {
            std::ostringstream oss;
            oss << elapsed_ms() << " ms";
            return oss.str();
        }

        // Reset to now.
        void reset() { ::gettimeofday(&_start, NULL); }

    private:
        struct timeval _start;
    };

private:

    Logger() : _tee(NULL), _orig_cerr(NULL), _min_level(INFO) {}
    ~Logger() { close(); }

    Logger(const Logger&);
    Logger& operator=(const Logger&);

    // Write a PERF/STATE tagged line directly to the log file
    // (bypasses the tee so it does NOT appear on the terminal).
    void _writeTagged(const char* tag, const std::string& msg)
    {
        if (!_file.is_open())
            return;
        time_t     now = ::time(NULL);
        struct tm* gmt = ::gmtime(&now);
        char       buf[24];
        ::strftime(buf, sizeof(buf), "[%Y-%m-%d %H:%M:%S] ", gmt);
        _file << buf << "[" << tag << "] " << msg << "\n";
        _file.flush();
    }

    static const char* _levelTag(Level l)
    {
        switch (l) {
            case DEBUG: return "DEBUG";
            case INFO:  return "INFO ";
            case WARN:  return "WARN ";
            case ERROR: return "ERROR";
            case PERF:  return "PERF ";
            case STATE: return "STATE";
            default:    return "?????";
        }
    }

    std::string     _path;
    std::ofstream   _file;
    TeeStreambuf*   _tee;
    std::streambuf* _orig_cerr;
    Level           _min_level;
};

#endif // LOGGER_HPP
