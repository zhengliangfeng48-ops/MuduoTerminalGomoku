#include "muduo/base/TimeZone.h"
#include "muduo/base/noncopyable.h"
#include "muduo/base/Date.h"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <assert.h>
#include <endian.h>

#include <stdint.h>
#include <stdio.h>

using namespace muduo;

struct TimeZone::Data{

    // LocalTime 为时区字典，Transition 为转换时的时间、索引
    // 当前 Transition 后的时区为 localtimes[localtimeIdx] 
    struct Transition{
        int64_t utctime;
        int64_t localtime;  // Shifted Epoch
        int localtimeIdx;

        Transition(int64_t t, int64_t l, int localIdx)
            : utctime(t), localtime(l), localtimeIdx(localIdx){}
    };

    struct LocalTime{
        int32_t utcOffset;  // East of UTC
        bool isDst;         //夏令时?
        int desigIdx;

        LocalTime(int32_t offset, bool dst, int idx)
            : utcOffset(offset), isDst(dst), desigIdx(idx){}
    };

    void addLocalTime(int32_t utcOffset, bool isDst, int desigIdx){
        localtimes.push_back(LocalTime(utcOffset, isDst, desigIdx));
    }

    void addTransition(int64_t utcTime, int localtimeIdx){
        LocalTime lt = localtimes.at(localtimeIdx);
        transitions.push_back(Transition(utcTime,utcTime + lt.utcOffset, localtimeIdx));
    }

    const LocalTime* findLocalTime(int64_t utcTime) const;
    const LocalTime* findLocalTime(const struct DateTime& local,bool postTransition) const;

    struct CompareUtcTime{
        bool operator()(const Transition& lhs, const Transition& rhs) const{
            return lhs.utctime < rhs.utctime;
        }
    };

    struct CompareLocalTime{
        bool operator()(const Transition& lhs, const Transition& rhs) const{
            return lhs.localtime < rhs.localtime;
        }
    };

    std::vector<Transition> transitions;
    std::vector<LocalTime> localtimes;
    string abbreviation;    // 时区名称缩写
    string tzstring;        // timezone string
};

namespace muduo{

const int kSecondsPerDay = 24*60*60;

namespace detail{

class File : noncopyable{
public:
    File(const char* file) : fp_(::fopen(file,"rb")){}

    ~File(){
        if(fp_){
            ::fclose(fp_);
        }
    }

    bool valid() const { return fp_; }

    string readBytes(int n){
        char buf[n];
        ssize_t nr = ::fread(buf, 1, n, fp_);
        if(nr!=n)
            throw std::logic_error("no enough data");
        return string(buf, n);      // 拷贝buf的前n项构造string
    }

    string readToEnd(){
        char buf[4096];
        string result;
        ssize_t nr=0;
        while((nr = ::fread(buf,1,sizeof buf, fp_))>0){
            result.append(buf, nr);     // buf的前nr项追加到result末尾
        }
        return result;
    }

    int64_t readInt64(){
        int64_t x=0;
        ssize_t nr = ::fread(&x, 1, sizeof(int64_t), fp_);
        if(nr != sizeof(int64_t))
            throw std::logic_error("bad int64_t data");
        return be64toh(x);
    }

    int32_t readInt32(){
        int32_t x = 0;
        ssize_t nr = ::fread(&x, 1, sizeof(int32_t), fp_);
        if (nr != sizeof(int32_t))
            throw std::logic_error("bad int32_t data");
        return be32toh(x);
    }

    uint8_t readUInt8(){
        uint8_t x = 0;
        ssize_t nr = ::fread(&x, 1, sizeof(uint8_t), fp_);
        if (nr != sizeof(uint8_t))
            throw std::logic_error("bad uint8_t data");
        return x;
    }

    off_t skip(ssize_t bytes){
        return ::fseek(fp_, bytes, SEEK_CUR);   //成功0, 失败-1 & errno
    }

private:
    FILE* fp_;
};

// RFC 8536: https://www.rfc-editor.org/rfc/rfc8536.html
// 解析 TZif(Time Zone Information Format)时区文件数据块
// 按照 TZif 规范（RFC 8536/9636）的二进制布局，依次读取并重建时区规则，
// 最终填充到 TimeZone::Data 中，供后续 UTC 与本地时间转换使用
bool readDataBlock(File& f, struct TimeZone::Data* data, bool v1){
    const int time_size = v1 ? sizeof(int32_t) : sizeof(int64_t);
    const int32_t isutccnt = f.readInt32();
    const int32_t isstdcnt = f.readInt32();
    const int32_t leapcnt = f.readInt32();
    const int32_t timecnt = f.readInt32();
    const int32_t typecnt = f.readInt32();
    const int32_t charcnt = f.readInt32();

    if (leapcnt != 0)   // muduo 不支持闰秒,非零直接返回失败
        return false;

    // isutccnt 和 isstdcnt 要么为 0，要么必须等于 typecnt，否则数据不合法
    if (isutccnt != 0 && isutccnt != typecnt)
        return false;
    if (isstdcnt != 0 && isstdcnt != typecnt)
        return false;
    
    // 循环读取 timecnt 个 UTC 切换时间，存入 trans 向量。
    // 这些时间是“本地时间规则可能发生变化的时刻”，按规范应严格升序排列。
    std::vector<int64_t> trans;
    trans.reserve(timecnt);
    for (int i = 0; i < timecnt; ++i){
        if (v1){
            trans.push_back(f.readInt32());
        }else{
            trans.push_back(f.readInt64());
        }
    }

    // 读取 timecnt 个单字节无符号整数，存入 localtimes。
    // 每个值是一个索引，指向后面 typecnt 个本地时间类型中的某一个，
    // 表示从对应切换时间开始应使用哪个本地时间规则。
    std::vector<int> localtimes;
    localtimes.reserve(timecnt);
    for (int i = 0; i < timecnt; ++i){
        uint8_t local = f.readUInt8();
        localtimes.push_back(local);
    }

    /*
    每个本地时间类型记录固定 6 字节:
    gmtoff（4 字节有符号整数，加到 UTC 上得到本地时间）、
    isdst（1 字节，是否夏令时）、
    abbrind（1 字节，时区缩写字符串的索引）
    */
    data->localtimes.reserve(typecnt);
    for (int i = 0; i < typecnt; ++i){
        int32_t gmtoff = f.readInt32();
        uint8_t isdst = f.readUInt8();
        uint8_t abbrind = f.readUInt8();

        data->addLocalTime(gmtoff, isdst, abbrind);
    }

    for (int i = 0; i < timecnt; ++i){
        //把每个切换时间与其对应的本地时间类型关联起来
        int localIdx = localtimes[i];
        data->addTransition(trans[i], localIdx);
    }

    // charcnt 指定了缩写字符串区的总字节数，函数把这部分读入 data->abbreviation。
    // 本地时间类型中的 abbrind 就是在这个字节数组中的偏移，用来定位诸如 "CST"、"PDT" 这样的缩写。
    data->abbreviation = f.readBytes(charcnt);

    // 跳过闰秒记录、标准/墙钟指示器、UT/本地指示器
    f.skip(leapcnt * (time_size + 4));
    f.skip(isstdcnt);
    f.skip(isutccnt);

    // 对 v2+ 读取尾部 TZ 字符串
    /*
    这部分通常是 POSIX TZ 风格的字符串，用于描述最后一次切换之后的规则。
    代码中的 FIXME 注释说明这里只是简单地把剩余内容全部读入，并未做严格解析。
    对于 muduo 限定的 1970~2100 年范围来说，
    核心转换主要依赖前面读到的切换表和规则，尾部字符串更多是保留信息。
    */
    if (!v1){
        // FIXME: read to next new-line.
        data->tzstring = f.readToEnd();
    }

    return true;
}

bool readTimeZoneFile(const char* zonefile, struct TimeZone::Data* data){
    File f(zonefile);
    if(f.valid()){
        try{
            string head = f.readBytes(4);
            if (head != "TZif")
                throw std::logic_error("bad head");
            string version = f.readBytes(1);
            f.readBytes(15);

            const int32_t isgmtcnt = f.readInt32();
            const int32_t isstdcnt = f.readInt32();
            const int32_t leapcnt = f.readInt32();
            const int32_t timecnt = f.readInt32();
            const int32_t typecnt = f.readInt32();
            const int32_t charcnt = f.readInt32();

            if (version == "2"){
                size_t skip = sizeof(int32_t) * timecnt + timecnt + 6 * typecnt +
                    charcnt +  8 * leapcnt + isstdcnt + isgmtcnt;
                f.skip(skip);

                head = f.readBytes(4);
                if (head != "TZif")
                    throw std::logic_error("bad head");
                f.skip(16);
                return readDataBlock(f, data, false);
            }else{
                // TODO: Test with real v1 file.
                f.skip(-4 * 6);  // Rewind to counters
                return readDataBlock(f, data, true);
            }
        }catch (std::logic_error& e){
            fprintf(stderr, "%s\n", e.what());
        }
    }
    return false;
}

inline void fillHMS(unsigned seconds, struct DateTime* dt){
    dt->second = seconds % 60;
    unsigned minutes = seconds / 60;
    dt->minute = minutes % 60;
    dt->hour = minutes / 60;
}

DateTime BreakTime(int64_t t){
    struct DateTime dt;
    int seconds = static_cast<int>(t % kSecondsPerDay);
    int days = static_cast<int>(t / kSecondsPerDay);
    // C++11 rounds towards zero.
    if (seconds < 0){
        seconds += kSecondsPerDay;
        --days;
    }
    detail::fillHMS(seconds, &dt);
    Date date(days + Date::kJulianDayOf1970_01_01); 
    // 所以传参是相对1970-01-01的时间
    Date::YearMonthDay ymd = date.yearMonthDay();
    dt.year = ymd.year;
    dt.month = ymd.month;
    dt.day = ymd.day;

    return dt;
}

}   // namespace detail

}   // namespace muduo

const TimeZone::Data::LocalTime* TimeZone::Data::findLocalTime(int64_t utcTime) const{
    const LocalTime* local = NULL;

    // row UTC time             isdst  offset  Local time (PRC)
    //  1  1989-09-16 17:00:00Z   0      8.0   1989-09-17 01:00:00
    //  2  1990-04-14 18:00:00Z   1      9.0   1990-04-15 03:00:00
    //  3  1990-09-15 17:00:00Z   0      8.0   1990-09-16 01:00:00
    //  4  1991-04-13 18:00:00Z   1      9.0   1991-04-14 03:00:00
    //  5  1991-09-14 17:00:00Z   0      8.0   1991-09-15 01:00:00

    // input '1990-06-01 00:00:00Z', std::upper_bound returns row 3,
    // so the input is in range of row 2, offset is 9 hours,
    // local time is 1990-06-01 09:00:00
    if (transitions.empty() || utcTime < transitions.front().utctime){
        // FIXME: should be first non dst time zone
        local = &localtimes.front();
    }else{
        Transition sentry(utcTime, 0, 0);
        std::vector<Transition>::const_iterator transI =
            std::upper_bound(transitions.begin(), transitions.end(), sentry, CompareUtcTime());
        assert(transI != transitions.begin());
        if (transI != transitions.end()){
            --transI;
            local = &localtimes[transI->localtimeIdx];
        }else{
            // FIXME: use TZ-env
            local = &localtimes[transitions.back().localtimeIdx];
        }
    }
    return local;
}

const TimeZone::Data::LocalTime* TimeZone::Data::findLocalTime(
    const struct DateTime& lt, bool postTransition) const {

    const int64_t localtime = fromUtcTime(lt);

    if (transitions.empty() || localtime < transitions.front().localtime){
        // FIXME: should be first non dst time zone
        return &localtimes.front();
    }

    Transition sentry(0, localtime, 0);
    std::vector<Transition>::const_iterator transI =
        std::upper_bound(transitions.begin(), transitions.end(), sentry, CompareLocalTime());
    assert(transI != transitions.begin());

    if (transI == transitions.end()){
        // FIXME: use TZ-env
        return &localtimes[transitions.back().localtimeIdx];
    }

    Transition prior_trans = *(transI - 1);
    int64_t prior_second = transI->utctime - 1 + localtimes[prior_trans.localtimeIdx].utcOffset;

    // row UTC time             isdst  offset  Local time (PRC)     Prior second local time
    //  1  1989-09-16 17:00:00Z   0      8.0   1989-09-17 01:00:00
    //  2  1990-04-14 18:00:00Z   1      9.0   1990-04-15 03:00:00  1990-04-15 01:59:59
    //  3  1990-09-15 17:00:00Z   0      8.0   1990-09-16 01:00:00  1990-09-16 01:59:59
    //  4  1991-04-13 18:00:00Z   1      9.0   1991-04-14 03:00:00  1991-04-14 01:59:59
    //  5  1991-09-14 17:00:00Z   0      8.0   1991-09-15 01:00:00

    // 以下为两种歧义情况

    // input 1991-04-14 02:30:00, found row 4,
    //  4  1991-04-13 18:00:00Z   1      9.0   1991-04-14 03:00:00  1991-04-14 01:59:59
    if (prior_second < localtime){      // 说明 localtime 跳了1小时
        // 夏令时春季 02:00:00 到 03:00:00 被跳过了
        // it's a skip
        // printf("SKIP: prev %ld local %ld start %ld\n", prior_second, localtime, transI->localtime);
        if (postTransition){
            return &localtimes[transI->localtimeIdx];
        }else{
            return &localtimes[prior_trans.localtimeIdx];
        }
    }

    // input 1990-09-16 01:30:00, found row 4, looking at row 3
    //  3  1990-09-15 17:00:00Z   0      8.0   1990-09-16 01:00:00  1990-09-16 01:59:59
    --transI;
    if (transI != transitions.begin()){
        prior_trans = *(transI - 1);
        prior_second = transI->utctime - 1 + localtimes[prior_trans.localtimeIdx].utcOffset;
    }
    if (localtime <= prior_second){     // 说明 localtime 回调了1小时
        // 夏令时秋季 02:00:00 到 03:00:00 重复一次 
        // it's repeat
        // printf("REPEAT: prev %ld local %ld start %ld\n", prior_second, localtime, transI->localtime);
        if (postTransition){
            return &localtimes[transI->localtimeIdx];
        }else{
            return &localtimes[prior_trans.localtimeIdx];
        }
    }

    // otherwise, it's unique
    return &localtimes[transI->localtimeIdx];
}

// static
TimeZone TimeZone::UTC(){
    return TimeZone(0, "UTC");
}

// static
TimeZone TimeZone::loadZoneFile(const char* zonefile){
    std::unique_ptr<Data> data(new Data);
    if (!detail::readTimeZoneFile(zonefile, data.get())){
        data.reset();
    }
    return TimeZone(std::move(data));
}

TimeZone::TimeZone(std::unique_ptr<Data> data)
    : data_(std::move(data)){}

TimeZone::TimeZone(int eastOfUtc, const char* name)
    : data_(new TimeZone::Data){
    data_->addLocalTime(eastOfUtc, false, 0);
    data_->abbreviation = name;
}

struct DateTime TimeZone::toLocalTime(int64_t seconds, int* utcOffset) const{
    struct DateTime localTime;
    assert(data_ != NULL);

    const Data::LocalTime* local = data_->findLocalTime(seconds);

    if (local){
        localTime = detail::BreakTime(seconds + local->utcOffset);
        if (utcOffset){
            *utcOffset = local->utcOffset;
        }
    }

    return localTime;
}

int64_t TimeZone::fromLocalTime(const struct DateTime& localtime, bool postTransition) const{
    assert(data_ != NULL);
    const Data::LocalTime* local = data_->findLocalTime(localtime, postTransition);
    const int64_t localSeconds = fromUtcTime(localtime);
    if (local){
        return localSeconds - local->utcOffset;
    }
    // fallback as if it's UTC time.
    return localSeconds;
}

DateTime TimeZone::toUtcTime(int64_t secondsSinceEpoch){
    return detail::BreakTime(secondsSinceEpoch);
}

int64_t TimeZone::fromUtcTime(const DateTime& dt){
    Date date(dt.year, dt.month, dt.day);
    int secondsInDay = dt.hour * 3600 + dt.minute * 60 + dt.second;
    int64_t days = date.julianDayNumber() - Date::kJulianDayOf1970_01_01;
    return days * kSecondsPerDay + secondsInDay;
}

DateTime::DateTime(const struct tm& t)
    : year(t.tm_year + 1900), month(t.tm_mon + 1), day(t.tm_mday),
    hour(t.tm_hour), minute(t.tm_min), second(t.tm_sec){}

string DateTime::toIsoString() const {
    char buf[64];
    snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d",
            year, month, day, hour, minute, second);
    return buf;
}
