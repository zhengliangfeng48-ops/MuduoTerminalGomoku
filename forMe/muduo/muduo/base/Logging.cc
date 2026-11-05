Logger(SourceFile file, int line);
Logger(SourceFile file, int line, LogLevel level);
Logger(SourceFile file, int line, LogLevel level, const char* func);
Logger(SourceFile file, int line, bool toAbort);
~Logger();

static void setLogLevel(LogLevel level);

typedef void (*OutputFunc)(const char* msg, int len);
typedef void (*FlushFunc)();
static void setOutput(OutputFunc);
static void setFlush(FlushFunc);
static void setTimeZone(const TimeZone& tz);

Impl(LogLevel level, int old_errno, const SourceFile& file, int line);
void formatTime();
void finish();

extern Logger::LogLevel g_logLevel;

const char* strerror_tl(int savedErrno);