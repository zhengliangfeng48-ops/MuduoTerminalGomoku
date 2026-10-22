struct DateTime :
    explicit DateTime(const struct tm&); 
    string toIsoString() const;

class TimeZone :
    TimeZone(int eastOfUtc, const char* tzname);  
    static TimeZone UTC();
    static TimeZone China();
    static TimeZone loadZoneFile(const char* zonefile);
    struct DateTime toLocalTime(int64_t secondsSinceEpoch, int* utcOffset = nullptr) const;
    int64_t fromLocalTime(const struct DateTime&, bool postTransition = false) const;
    
    explicit TimeZone(std::unique_ptr<Data> data);