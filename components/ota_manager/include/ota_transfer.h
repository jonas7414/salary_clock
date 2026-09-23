#pragma once
#include <cstddef>
#include <cstdint>
namespace ota {
enum class TransferResult { Success, Incomplete, Oversize, WriteFailed, DigestFailed, ImageFailed, ActivateFailed };
// Caller validates the prefix, opens only the inactive partition and owns abort
// cleanup. This ordering prevents activation until every verification succeeds.
template<class Read,class Write,class Verify,class Finish,class Activate,class Progress>
TransferResult transfer_image(uint8_t *buffer,size_t capacity,size_t prefix,uint32_t expected,
    Read read,Write write,Verify verify,Finish finish,Activate activate,Progress progress) {
    uint32_t received=0;
    int count=static_cast<int>(prefix);
    for (;;) {
        if (count<0) return TransferResult::Incomplete;
        if (!count) break;
        if (size_t(count)>capacity || uint64_t(received)+count>expected) return TransferResult::Oversize;
        if (!write(buffer,size_t(count))) return TransferResult::WriteFailed;
        received+=count; progress(received,expected);
        count=read(buffer,capacity);
    }
    if (received!=expected) return TransferResult::Incomplete;
    if (!verify()) return TransferResult::DigestFailed;
    if (!finish()) return TransferResult::ImageFailed;
    if (!activate()) return TransferResult::ActivateFailed;
    return TransferResult::Success;
}
}
