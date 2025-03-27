#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    , _timer(retx_timeout) {}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

void TCPSender::fill_window() {
    uint16_t window_size = max(_window_size, static_cast<uint16_t>(1));
    while (_bytes_in_flight < window_size) {
        TCPSegment seg;
        if (!_set_syn_flag) {
            seg.header().syn = true;
            _set_syn_flag = true;
        }

        uint16_t payload_size = min(TCPConfig::MAX_PAYLOAD_SIZE, 
                                min(window_size - _bytes_in_flight - seg.header().syn, 
                                    _stream.buffer_size()));
        string payload = _stream.read(payload_size);
        seg.payload() = Buffer(move(payload));

        if (!_set_fin_flag && _stream.eof() 
        && _bytes_in_flight + seg.length_in_sequence_space() < window_size) {
            seg.header().fin = true;
            _set_fin_flag = true;
        }

        size_t seg_len = seg.length_in_sequence_space();
        if(seg_len == 0) break;

        seg.header().seqno = next_seqno();
        _segments_out.push(seg);

        if(!_timer.is_running()) _timer.restart();

        _outstanding_seg.emplace(_next_seqno, move(seg));

        _next_seqno += seg_len;
        _bytes_in_flight += seg_len;
    }


}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) { 
    uint64_t abs_ackno = unwrap(ackno, _isn, next_seqno_absolute());
    if (abs_ackno > next_seqno_absolute()) return;
    bool win = false;

    while (!_outstanding_seg.empty()) {
        auto &[abs_seqno, seg] = _outstanding_seg.front();
        if (abs_seqno + seg.length_in_sequence_space() - 1 < abs_ackno) {
            win = true;
            _bytes_in_flight -= seg.length_in_sequence_space();
            _outstanding_seg.pop();
        } else {
            break;
        }
    }

    if (win) {
        _consecutive_retransmissions_count = 0;
        _timer.set_time_out(_initial_retransmission_timeout);
        _timer.restart();
    }

    if (_bytes_in_flight == 0) {
        _timer.stop();
    }

    _window_size = window_size;
    fill_window();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) { 
    _timer.tick(ms_since_last_tick);

    if (_timer.check_time_out() && !_outstanding_seg.empty()) {
        _segments_out.push(_outstanding_seg.front().second);

        if (_window_size > 0) {
            ++_consecutive_retransmissions_count;
            _timer.set_time_out(_timer.get_time_out() * 2);
        }

        _timer.restart();
    }    
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmissions_count; }

void TCPSender::send_empty_segment() {
    TCPSegment seg;
    seg.header().seqno = next_seqno();
    _segments_out.emplace(move(seg));
}
