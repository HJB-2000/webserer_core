// ============================================================
//  Logger.hpp  —  log file handler
//
//  Strategy: TeeStreambuf intercepts std::cerr and mirrors
//  every byte to both the original terminal AND a log file.
//
//  Every line written to the log file is prefixed with an
//  ISO-8601 UTC timestamp:
//    [2024-11-04 12:00:00] [EventLoop] starting
//
//  The terminal output is unchanged (no timestamp injected)
//  so it stays readable during development.
//
//  Usage in main():
//    Logger::instance().open("webserv.log");  // at startup
//    ...
//    Logger::instance().close();              // before return
//
//  After open(), ALL std::cerr output in every translation
//  unit is automatically captured — no other file needs
//  to be changed.
//
//  C++98 compliant.  No external dependencies.
// ============================================================
#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <streambuf>
#include <fstream>
#include <iostream>
#include <string>
#include <ctime>


// ────────────────────────────────────────────────────────────
//  TeeStreambuf
//
//  A custom streambuf that sits between std::cerr and its
//  original destination.  Each character is forwarded to:
//    1. _orig  — the original stderr streambuf (terminal)
//    2. _file  — the open log file
//
//  A UTC timestamp is injected at the start of every new
//  line in the log file so each entry is self-contained.
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

    // Called for every character that cannot fit in the put buffer.
    // Since we never set up a put buffer, every character comes here.
    virtual int overflow(int c)
    {
        if (c == EOF)
            return c;

        const char ch = static_cast<char>(c);

        // ── 1. Mirror to terminal (original stderr) ───────────
        _orig->sputc(ch);

        // ── 2. Write to log file (with timestamp on new lines) ─
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
            _file.flush();  // flush after each line → real-time  tail -f
        }

        return c;
    }

    // Override xsputn so bulk writes (e.g. large strings) also go
    // through our overflow() logic character by character.
    virtual std::streamsize xsputn(const char* s, std::streamsize n)
    {
        for (std::streamsize i = 0; i < n; ++i)
            overflow(static_cast<unsigned char>(s[i]));
        return n;
    }

private:

    // Returns "[YYYY-MM-DD HH:MM:SS] " in UTC
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
//
//  Owns the TeeStreambuf and the log file.
//  open() installs the tee; close() restores the original.
// ────────────────────────────────────────────────────────────
class Logger
{
public:

    // Returns the single Logger instance.
    static Logger& instance()
    {
        static Logger inst;
        return inst;
    }

    // Install the tee and open the log file.
    // Safe to call only once.  If the file cannot be opened,
    // a warning is printed and logging continues to stderr only.
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

        // Install TeeStreambuf: intercept std::cerr from this point on.
        _orig_cerr = std::cerr.rdbuf();
        _tee       = new TeeStreambuf(_orig_cerr, _file);
        std::cerr.rdbuf(_tee);

        // Session start marker (goes through the tee, so it is
        // timestamped and written to both terminal and log file).
        std::cerr << "========================================"
                     "========================================\n"
                  << "[Logger] log session started  —  file: " << path << "\n"
                  << "========================================"
                     "========================================\n";
    }

    // Restore original stderr and close the log file.
    // Called explicitly from main() before exit.
    void close()
    {
        if (!_tee)
            return;

        std::cerr << "========================================"
                     "========================================\n"
                  << "[Logger] log session ended\n"
                  << "========================================"
                     "========================================\n\n";

        // Restore original stderr BEFORE deleting the tee so that
        // any output after this point goes only to the terminal.
        std::cerr.rdbuf(_orig_cerr);
        delete _tee;
        _tee       = NULL;
        _orig_cerr = NULL;

        _file.close();
    }

    const std::string& path() const { return _path; }

private:

    // Singleton — no public construction
    Logger() : _tee(NULL), _orig_cerr(NULL) {}
    ~Logger() { close(); }

    // non-copyable
    Logger(const Logger&);
    Logger& operator=(const Logger&);

    std::string     _path;
    std::ofstream   _file;
    TeeStreambuf*   _tee;
    std::streambuf* _orig_cerr;
};

#endif // LOGGER_HPP
