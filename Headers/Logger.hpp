#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <streambuf>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <ctime>


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


class Logger
{
public:

    enum Level { DEBUG = 0, INFO, WARN, ERROR, PERF, STATE };
    static Logger& instance();


    void open(const std::string& path);
    void close();

    void  setLevel(Level min);
    Level getLevel() const;

    void log(Level level, const std::string& msg);

    void perf(const std::string& msg);

    void state(const std::string& msg);

    const std::string& path() const;

    class Stopwatch
    {
    public:
        Stopwatch();

        double elapsed_ms() const;

        std::string str() const;

    void reset();

    private:
        time_t _start;
    };

private:

    Logger();
    ~Logger();

    Logger(const Logger&);
    Logger& operator=(const Logger&);

    void _writeTagged(const char* tag, const std::string& msg);

    static const char* _levelTag(Level l);

    std::string     _path;
    std::ofstream   _file;
    TeeStreambuf*   _tee;
    std::streambuf* _orig_cerr;
    Level           _min_level;
};
int readSomaxconn();
#endif
