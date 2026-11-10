#include "muduo/base/Logging.h"
#include "muduo/base/CurrentThread.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/syscall.h>
#include <unistd.h>

// Temporary stub for cacheTid (not yet implemented in CurrentThread.cc)
namespace muduo {
namespace CurrentThread {
void cacheTid() {
    if (t_cachedTid == 0) {
        t_cachedTid = static_cast<int>(::syscall(SYS_gettid));
        snprintf(t_tidString, sizeof(t_tidString), "%5d ", t_cachedTid);
        t_tidStringLength = static_cast<int>(strlen(t_tidString));
    }
}
} // namespace CurrentThread
} // namespace muduo

void testOutput(const char* msg, int len) {
    fwrite(msg, 1, len, stdout);
}

int main() {
    muduo::Logger::setOutput(testOutput);

    printf("=== Muduo Logging Test ===\n\n");

    printf("[Test 1] Log levels at default (INFO)\n");
    LOG_TRACE << "TRACE - should NOT appear";
    LOG_DEBUG << "DEBUG - should NOT appear";
    LOG_INFO  << "INFO  - should appear";
    LOG_WARN  << "WARN  - should appear";
    LOG_ERROR << "ERROR - should appear";

    printf("\n[Test 2] Set level to TRACE\n");
    muduo::Logger::setLogLevel(muduo::Logger::TRACE);
    LOG_TRACE << "TRACE now visible";
    LOG_DEBUG << "DEBUG now visible";
    LOG_INFO  << "INFO  still visible";

    printf("\n[Test 3] Stream various types\n");
    int         i = 42;
    double      d = 3.14159;
    const char* s = "hello muduo";
    std::string str = "std::string ok";
    LOG_INFO << "int=" << i << " double=" << d << " cstr=" << s << " string=" << str;

    printf("\n[Test 4] CHECK_NOTNULL\n");
    int val = 100;
    int* p = CHECK_NOTNULL(&val);
    LOG_INFO << "CHECK_NOTNULL passed, value=" << *p;

    printf("\n[Test 5] Set level to WARN\n");
    muduo::Logger::setLogLevel(muduo::Logger::WARN);
    LOG_INFO << "This INFO should NOT appear";
    LOG_WARN << "This WARN should appear";

    printf("\n=== All logging tests completed ===\n");
    return 0;
}
