typedef std::function<void ()> ThreadFunc;

explicit Thread(ThreadFunc, const string& name = string());
~Thread();

void start();
int join(); // return pthread_join()


void setDefaultName();