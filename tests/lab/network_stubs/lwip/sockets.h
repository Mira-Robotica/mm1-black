#pragma once
#include <string>
#include <cerrno>
#include <algorithm>
constexpr int MSG_DONTWAIT=1;
inline int send_limit=2048,send_error=0;
inline std::string sent_bytes;
inline int send(int,const void *data,size_t size,int) {
    if(send_error) { errno=send_error;return -1; }
    const int n=std::min(size,size_t(send_limit));
    sent_bytes.append(static_cast<const char*>(data),n);return n;
}
