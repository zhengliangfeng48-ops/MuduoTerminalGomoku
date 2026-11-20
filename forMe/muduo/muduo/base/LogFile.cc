/*      TodoList:
LogFile(const string& basename,
            off_t rollSize,
            bool threadSafe = true,
            int flushInterval = 3,
            int checkEveryN = 1024);
~LogFile();

void append(const char* logline, int len);
void flush();
bool rollFile();

void append_unlocked(const char* logline, int len);

static string getLogFileName(const string& basename, time_t* now);
*/