#pragma once

#include <stddef.h>

// Fixed storage; consume fragmented TCP/serial input without waiting for a line.
class LabLineReader {
public:
    enum class Result { Pending, Line, TooLong, Invalid };
    static constexpr size_t kMaxLength = 128;

    Result push(char ch)
    {
        if (ch == '\n') {
            const Result result = overflow_ ? Result::TooLong
                                  : invalid_ ? Result::Invalid : Result::Line;
            data_[length_] = '\0';
            length_ = 0;
            overflow_ = false;
            invalid_ = false;
            return result;
        }
        if (ch == '\r') {
            return Result::Pending;
        }
        if (ch < ' ' || ch > '~') {
            invalid_ = true;
        }
        if (length_ == kMaxLength) {
            overflow_ = true;
        } else {
            data_[length_++] = ch;
        }
        return Result::Pending;
    }

    const char *line() const { return data_; }
    void reset() { *this = LabLineReader(); }

private:
    char data_[kMaxLength + 1]{};
    size_t length_ = 0;
    bool overflow_ = false;
    bool invalid_ = false;
};
