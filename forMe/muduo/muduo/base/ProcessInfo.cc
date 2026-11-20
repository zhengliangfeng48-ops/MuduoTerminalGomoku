#include "muduo/base/ProcessInfo.h"
#include "muduo/base/CurrentThread.h"
#include "muduo/base/FileUtil.h"

#include <algorithm>

#include <assert.h>
#include <dirent.h>
#include <pwd.h>
#include <stdio.h> // snprintf
#include <stdlib.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/times.h>

namespace muduo{

namespace detail{

__thread int t_numOpenedFiles = 0;
int fdDirFilter(const struct dirent* d){
    // 通过判断首字符是否为数字过滤出fd文件 (/proc/<pid>/fd/<纯数字>)
    if (::isdigit(d->d_name[0])){
        ++t_numOpenedFiles;
    }
    return 0;
}

__thread std::vector<pid_t>* t_pids = NULL;
int taskDirFilter(const struct dirent* d){
    // 过滤pid (/proc/<纯数字>)
    if (::isdigit(d->d_name[0])){
        t_pids->push_back(atoi(d->d_name));
    }
    return 0;
}

// 以上两个固定返回0的函数不用 void, 是因为 scandir 的回调签名固定返回值为 int
int scanDir(const char *dirpath, int (*filter)(const struct dirent *)){
    struct dirent** namelist = NULL;
    /*  filter 返回非0则被过滤选中
    int scandir(const char *restrict dirp,
            struct dirent ***restrict namelist,
            int (*filter)(const struct dirent *),
            int (*compar)(const struct dirent **, const struct dirent **));
        alphasort 是 scandir 的配套排序函数, 基本按照该语言文字的字母顺序规则排序   */
    int result = ::scandir(dirpath, &namelist, filter, alphasort);
    assert(namelist == NULL);
    return result;
}

Timestamp g_startTime = Timestamp::now();
// assume those won't change during the life time of a process.
int g_clockTicks = static_cast<int>(::sysconf(_SC_CLK_TCK));
int g_pageSize = static_cast<int>(::sysconf(_SC_PAGE_SIZE));

}   // namespace detail

}   // namespace muduo

using namespace muduo;
using namespace muduo::detail;

pid_t ProcessInfo::pid(){
    return ::getpid();
}

string ProcessInfo::pidString(){
    char buf[32];
    snprintf(buf, sizeof buf, "%d", pid());
    return buf;
}

uid_t ProcessInfo::uid(){
    return ::getuid();
}

string ProcessInfo::username(){
    struct passwd pwd;
    struct passwd* result = NULL;
    char buf[8192];
    const char* name = "unknownuser";

    /*  pwd 的成员只是指针, 故需要 buffer, 否则无处可指
    int getpwuid_r(uid_t uid, struct passwd *pwd,
               char *buffer, size_t bufsize,
               struct passwd **result);
    成功找到记录时，*result 指向 pwd, 否则指向 NULL     */
    getpwuid_r(uid(), &pwd, buf, sizeof buf, &result);
    if (result){
        name = pwd.pw_name;
    }
    return name;
}

// 有效 uid
uid_t ProcessInfo::euid(){
    return ::geteuid();
}

Timestamp ProcessInfo::startTime(){
    return g_startTime;
}

int ProcessInfo::clockTicksPerSecond(){
    return g_clockTicks;
}

int ProcessInfo::pageSize(){
    return g_pageSize;
}

bool ProcessInfo::isDebugBuild(){
#ifdef NDEBUG
    return false;
#else
    return true;
#endif
}

// 获取当前计算机主机名
string ProcessInfo::hostname(){
    // HOST_NAME_MAX 64
    // _POSIX_HOST_NAME_MAX 255
    char buf[256];
    if (::gethostname(buf, sizeof buf) == 0){
        buf[sizeof(buf)-1] = '\0';
        return buf;
    }else{
        return "unknownhost";
    }
}

string ProcessInfo::procname(){
    return procname(procStat()).as_string();
}

StringPiece ProcessInfo::procname(const string& stat){
    StringPiece name;
    // stat 中有且只有名称字段是用小括号包着的
    size_t lp = stat.find('(');
    size_t rp = stat.rfind(')');

    // static const size_type npos = -1;
    if (lp != string::npos && rp != string::npos && lp < rp){
        name.set(stat.data()+lp+1, static_cast<int>(rp-lp-1));
    }
    return name;
}

// "self" 始终指向当前正在访问该路径的进程的 PID 目录
string ProcessInfo::procStatus(){
    string result;
    FileUtil::readFile("/proc/self/status", 65536, &result);
    return result;
}

string ProcessInfo::procStat(){
    string result;
    FileUtil::readFile("/proc/self/stat", 65536, &result);
    return result;
}

string ProcessInfo::threadStat(){
    char buf[64];
    snprintf(buf, sizeof buf, "/proc/self/task/%d/stat", CurrentThread::tid());
    string result;
    FileUtil::readFile(buf, 65536, &result);
    return result;
}

string ProcessInfo::exePath(){
    string result;
    char buf[1024];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof buf);
    if (n > 0){
        result.assign(buf, n);
    }
    return result;
}

int ProcessInfo::openedFiles(){
    t_numOpenedFiles = 0;
    scanDir("/proc/self/fd", fdDirFilter);
    return t_numOpenedFiles;
}

int ProcessInfo::maxOpenFiles(){
    struct rlimit rl;
    // RLIMIT_NOFILE: 打开文件描述符的最大数量
    // 成功0, 失败-1 && errno
    if (::getrlimit(RLIMIT_NOFILE, &rl)){
        return openedFiles();
    }else{
        // struct rlimit 有两个成员:
        // rlim_cur: 软限制, 内核实际执行的当前限制值
        // rlim_max: 硬限制, 软限制的上限. 非特权进程只能降低硬限制, 不能升高
        return static_cast<int>(rl.rlim_cur);
    }
}

ProcessInfo::CpuTime ProcessInfo::cpuTime(){
    ProcessInfo::CpuTime t;
    struct tms tms;
    // 成功滴答数，失败-1
    if (::times(&tms) >= 0){
        const double hz = static_cast<double>(clockTicksPerSecond());
        // tms_utime: 当前进程消耗的用户态 CPU 时间(滴答数)
        // tms_stime: 当前进程消耗的系统态 CPU 时间(滴答数)
        t.userSeconds = static_cast<double>(tms.tms_utime) / hz;
        t.systemSeconds = static_cast<double>(tms.tms_stime) / hz;
    }
    return t;
}

int ProcessInfo::numThreads(){
    int result = 0;
    string status = procStatus();
    size_t pos = status.find("Threads:");
    if (pos != string::npos){
        result = ::atoi(status.c_str() + pos + 8/* 跳过"Threads" */);
    }
    return result;
}

std::vector<pid_t> ProcessInfo::threads(){
    std::vector<pid_t> result;
    t_pids = &result;
    scanDir("/proc/self/task", taskDirFilter);
    t_pids = NULL;  // 无所谓, 只是在维护此指针在外部为空的约定
    std::sort(result.begin(), result.end());
    return result;
}
