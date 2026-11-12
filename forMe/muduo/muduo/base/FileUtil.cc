
/*              TodoList:
ReadSmallFile::
    ReadSmallFile(StringArg filename);
    ~ReadSmallFile();

    template<typename String>
    int readToString(int maxSize,
                    String* content,
                    int64_t* fileSize,
                    int64_t* modifyTime,
                    int64_t* createTime);

    int readToBuffer(int* size);


AppendFile::
    explicit AppendFile(StringArg filename);

    ~AppendFile();

    void append(const char* logline, size_t len);

    void flush();

    size_t write(const char* logline, size_t len);
*/