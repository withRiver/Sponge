#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    const TCPHeader& header = seg.header();
    if(!_syn_recv && !header.syn) {
        return;
    }
    if(header.syn) {
        _isn = header.seqno;
        _syn_recv = true;
    }

    uint64_t abs_ackno = _reassembler.stream_out().bytes_written() + 1;
    uint64_t abs_seqno = unwrap(header.seqno, _isn, abs_ackno);
    uint64_t stream_index = abs_seqno - 1 + header.syn;
    _reassembler.push_substring(seg.payload().copy(), stream_index, header.fin);
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if(!_syn_recv) {
        return nullopt;
    }
    uint64_t abs_ackno = _reassembler.stream_out().bytes_written() + 1;
    if(_reassembler.stream_out().input_ended()) {
        ++abs_ackno;
    }    
    return wrap(abs_ackno, _isn);
}

size_t TCPReceiver::window_size() const { return _capacity - _reassembler.stream_out().buffer_size(); }
