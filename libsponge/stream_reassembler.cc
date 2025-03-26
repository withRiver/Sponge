#include "stream_reassembler.hh"

#include <limits>
// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) 
    : _output(capacity)
    , _capacity(capacity)
    , buffer(capacity, 0)
    , bitmap(capacity, 0)
    , _eof_index(numeric_limits<size_t>::max())
    , _unass_base(0)
    , _unass_bytes(0) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    size_t st = max(index, _unass_base);
    size_t ed = min(index + data.size(), _unass_base + _capacity - _output.buffer_size());
    if(eof) {
        _eof_index = index + data.size();
    }
    if(index + data.size() <= _unass_base) {
        if(_unass_base == _eof_index) {
            _output.end_input();
        }
        return;
    }
    for(size_t i = st - _unass_base, j = st - index; i < ed - _unass_base; ++i, ++j) {
        if(!bitmap[i]) {
            buffer[i] = data[j];
            bitmap[i] = true;
            ++_unass_bytes;
        }
    } 
    string out {};
    while(bitmap.front()) {
        out += buffer.front();
        buffer.pop_front();
        bitmap.pop_front();
        buffer.push_back(0);
        bitmap.push_back(0);
        ++_unass_base, --_unass_bytes;
    }
    _output.write(out);
    if(_unass_base == _eof_index) {
        _output.end_input();
    }
}

size_t StreamReassembler::unassembled_bytes() const { return _unass_bytes; }

bool StreamReassembler::empty() const { return _unass_bytes == 0; }
