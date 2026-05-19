#include "Headers/Logger.hpp"

namespace {
bool isLeapYear(int year)
{
	if (year % 400 == 0) return true;
	if (year % 100 == 0) return false;
	return (year % 4 == 0);
}

void splitUtc(time_t t, int& year, int& mon, int& mday,
			  int& hour, int& min, int& sec)
{
	long long seconds = static_cast<long long>(t);
	if (seconds < 0)
		seconds = 0;

	long long days = seconds / 86400;
	long long rem  = seconds % 86400;

	hour = static_cast<int>(rem / 3600);
	rem %= 3600;
	min  = static_cast<int>(rem / 60);
	sec  = static_cast<int>(rem % 60);

	year = 1970;
	while (true)
	{
		int diy = isLeapYear(year) ? 366 : 365;
		if (days >= diy)
		{
			days -= diy;
			++year;
		}
		else
		{
			break;
		}
	}

	static const int month_days[] =
		{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

	mon = 0;
	for (; mon < 12; ++mon)
	{
		int dim = month_days[mon];
		if (mon == 1 && isLeapYear(year))
			dim = 29;
		if (days >= dim)
			days -= dim;
		else
			break;
	}
	mday = static_cast<int>(days) + 1;
}

void appendTwoDigits(std::ostringstream& oss, int value)
{
	if (value < 10)
		oss << '0';
	oss << value;
}

std::string formatUtcTimestamp()
{
	int year, mon, mday, hour, min, sec;
	splitUtc(::time(NULL), year, mon, mday, hour, min, sec);

	std::ostringstream oss;
	oss << '[' << year << '-';
	appendTwoDigits(oss, mon + 1);
	oss << '-';
	appendTwoDigits(oss, mday);
	oss << ' ';
	appendTwoDigits(oss, hour);
	oss << ':';
	appendTwoDigits(oss, min);
	oss << ':';
	appendTwoDigits(oss, sec);
	oss << "] ";
	return oss.str();
}
} // namespace

TeeStreambuf::TeeStreambuf(std::streambuf* orig, std::ofstream& file)
	: _orig(orig)
	, _file(file)
	, _at_line_start(true)
{}

int TeeStreambuf::overflow(int c)
{
	if (c == EOF)
		return c;

	const char ch = static_cast<char>(c);

	_orig->sputc(ch);

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

std::streamsize TeeStreambuf::xsputn(const char* s, std::streamsize n)
{
	for (std::streamsize i = 0; i < n; ++i)
		overflow(static_cast<unsigned char>(s[i]));
	return n;
}

std::string TeeStreambuf::_utcTimestamp()
{
	return formatUtcTimestamp();
}

Logger& Logger::instance()
{
	static Logger inst;
	return inst;
}

Logger::Logger() : _tee(NULL), _orig_cerr(NULL), _min_level(INFO) {}

Logger::~Logger()
{
	close();
}

void Logger::open(const std::string& path)
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

void Logger::close()
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

void Logger::setLevel(Level min)
{
	_min_level = min;
}

Logger::Level Logger::getLevel() const
{
	return _min_level;
}

void Logger::log(Level level, const std::string& msg)
{
	if (level < _min_level)
		return;
	std::cerr << "[" << _levelTag(level) << "] " << msg << "\n";
}

void Logger::perf(const std::string& msg)
{
	_writeTagged("PERF ", msg);
}

void Logger::state(const std::string& msg)
{
	_writeTagged("STATE", msg);
}

const std::string& Logger::path() const
{
	return _path;
}

Logger::Stopwatch::Stopwatch() : _start(::time(NULL)) {}

double Logger::Stopwatch::elapsed_ms() const
{
	time_t now = ::time(NULL);
	return static_cast<double>(now - _start) * 1000.0;
}

std::string Logger::Stopwatch::str() const
{
	std::ostringstream oss;
	oss << elapsed_ms() << " ms";
	return oss.str();
}

void Logger::Stopwatch::reset()
{
	_start = ::time(NULL);
}

void Logger::_writeTagged(const char* tag, const std::string& msg)
{
	if (!_file.is_open())
		return;
	_file << formatUtcTimestamp() << "[" << tag << "] " << msg << "\n";
	_file.flush();
}

const char* Logger::_levelTag(Level l)
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
