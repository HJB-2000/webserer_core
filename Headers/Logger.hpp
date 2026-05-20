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
    TeeStreambuf(std::streambuf* orig, std::ofstream& file);

protected:
    virtual int overflow(int c);
    virtual std::streamsize xsputn(const char* s, std::streamsize n);

private:
    static std::string _utcTimestamp();

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
    static Logger& instance();

    // ── Lifecycle ─────────────────────────────────────────────

    void open(const std::string& path);
    void close();

    // ── Level filter ──────────────────────────────────────────
    void  setLevel(Level min);
    Level getLevel() const;

    // ── Tagged log channels ───────────────────────────────────
    //
    // log()   — general: writes to log file + terminal via cerr
    // perf()  — performance data: log file only (no terminal noise)
    // state() — connection/server state transitions: log file only

    void log(Level level, const std::string& msg);

    // Performance: written directly to the log file only.
    // Use for timing data, throughput metrics, etc.
    void perf(const std::string& msg);

    // State transitions: written directly to the log file only.
    // Use for connection FSM transitions, accept/close events, etc.
    void state(const std::string& msg);

    const std::string& path() const;

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
        Stopwatch();

        // Elapsed milliseconds since construction.
        double elapsed_ms() const;

        // Returns "X.XXX ms" ready for embedding in a log message.
        std::string str() const;

        // Reset to now.
    void reset();

    private:
        time_t _start;
    };

private:

    Logger();
    ~Logger();

    Logger(const Logger&);
    Logger& operator=(const Logger&);

    // Write a PERF/STATE tagged line directly to the log file
    // (bypasses the tee so it does NOT appear on the terminal).
    void _writeTagged(const char* tag, const std::string& msg);

    static const char* _levelTag(Level l);

    std::string     _path;
    std::ofstream   _file;
    TeeStreambuf*   _tee;
    std::streambuf* _orig_cerr;
    Level           _min_level;
};
int readSomaxconn();
#endif // LOGGER_HPP
