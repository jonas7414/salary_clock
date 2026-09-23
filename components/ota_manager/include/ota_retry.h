#pragma once
#include <cstddef>

namespace ota {
struct ReadAttempt {
    int count;
    bool complete;
    bool transient;
};

// Retry only no-data timeouts on the same HTTP stream. Positive short reads
// belong to the caller immediately; replaying them would corrupt the image.
template<class Read,class Allowed,class Pause>
int read_with_retry(Read read,Allowed allowed,Pause pause,unsigned max_retries) {
    unsigned retries=0;
    while (allowed()) {
        const ReadAttempt result=read();
        if (!allowed()) return -1;
        if (result.count>0 || (result.count==0 && result.complete)) return result.count;
        if (!result.transient || retries==max_retries) return -1;
        pause(++retries);
    }
    return -1;
}

enum class DownloadAttempt { Success, Retryable, Failed };

// Each attempt owns a new TLS connection, OTA handle and hash state. Validation
// and flash errors are final; only transport failures may restart from byte zero.
template<class Attempt,class Allowed,class Pause>
bool download_with_retry(Attempt attempt,Allowed allowed,Pause pause,unsigned max_attempts) {
    for (unsigned number=1;number<=max_attempts && allowed();++number) {
        const auto result=attempt();
        if (result==DownloadAttempt::Success) return true;
        if (result!=DownloadAttempt::Retryable || number==max_attempts || !allowed()) return false;
        pause(number);
    }
    return false;
}
}
