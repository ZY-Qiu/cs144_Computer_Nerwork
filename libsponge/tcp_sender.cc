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

uint64_t TCPSender::bytes_in_flight() const { return _next_seqno - _send_base; }

void TCPSender::fill_window() {
    // cases where stop
    if (this->_window_size == 0 || this->_fin_flag ||
        (this->_syn_flag && this->_stream.buffer_empty() && !this->_stream.input_ended()))
        return;
    if (!this->_syn_flag) {
        // send the syn first
        TCPSegment seg;
        seg.header().syn = true;
        send_segment(seg);
        this->_syn_flag = true;
        return;
    }
    if (this->_stream.eof()) {
        // eof() means buffer_empty && eof, send only fin flag
        // fin flag takes one seqno, check the window size
        if(this->_window_size - bytes_in_flight() <= 0) return;
        TCPSegment seg;
        seg.header().fin = true;
        send_segment(seg);
        this->_fin_flag = true;
        return;
    }
    // now fill in the window in a loop
    for (size_t rem = this->_window_size - bytes_in_flight(); rem > 0 && !this->_stream.buffer_empty();) {
        TCPSegment seg;
        // the read function actually eat up what's inside the _stream.buffer, causing its length to decrease
        string str = this->_stream.read(min(rem, TCPConfig::MAX_PAYLOAD_SIZE));
        // str is actually a pointer, so construct by taking ownership of a string in Buffer() with std::move()
        seg.payload() = Buffer(std::move(str));
        // consider when fin flag is set, fin segment can still carry data
        if (this->_stream.eof() && (seg.length_in_sequence_space() + 1 <= rem)) {
            seg.header().fin = true;
            this->_fin_flag = true;
        }
        rem -= seg.length_in_sequence_space();
        send_segment(seg);
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    uint64_t absolute_ackno = unwrap(ackno, this->_isn, this->_send_base);
    if (absolute_ackno > this->_next_seqno)
        return;

    if (window_size == 0) {
        this->_window_size = 1;
        this->_window_size_zero = true;
    } else {
        this->_window_size = window_size;
        this->_window_size_zero = false;
    }
    if (absolute_ackno <= this->_send_base)
        return;
    this->_send_base = absolute_ackno;

    // pop all element in the copy of segment_out before ackno
    while (!this->_copy_segments_out.empty()) {
        TCPSegment seg = this->_copy_segments_out.front();
        if (unwrap(seg.header().seqno, this->_isn, this->_next_seqno) + seg.length_in_sequence_space() <=
            absolute_ackno) {
            this->_copy_segments_out.pop();
        } else
            break;
    }

    fill_window();

    // reset timer and count of consecutive retransmissions
    this->_consecutive_retransmissions = 0;
    if (!this->_copy_segments_out.empty()) {
        this->_timer.start(this->_initial_retransmission_timeout);
    } else  // stop timer if there is no bytes in flight
        this->_timer.clear();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    // Periodically, the owner of the TCPSender will call the TCPSender’s tick method, indicating the passage of time.
    this->_timer.total_time += ms_since_last_tick;
    // check if _copy_segments_out is not empty && is timeout && timer running
    if (!this->_copy_segments_out.empty() && this->_timer.running && this->_timer.is_timeout()) {
        // retransmission
        this->_segments_out.push(_copy_segments_out.front());
        // receiver window size is not zero, meaning this retransmission is meaningful
        if (!this->_window_size_zero) {
            this->_consecutive_retransmissions++;
            this->_timer.double_rto();
        }
        this->_timer.start();
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmissions; }

void TCPSender::send_empty_segment() {
    TCPSegment seg;
    seg.header().seqno = wrap(this->_next_seqno, this->_isn);
    seg.payload() = {};
    this->_segments_out.push(seg);
}

// send the segment which consumes seqno and needs to be retransmitted if timeout
void TCPSender::send_segment(TCPSegment &seg) {
    seg.header().seqno = wrap(this->_next_seqno, this->_isn);
    this->_next_seqno += seg.length_in_sequence_space();
    // payload and part of header is preprocessed outside of this function
    this->_segments_out.push(seg);
    this->_copy_segments_out.push(seg);
    // start timer if not running
    if (!this->_timer.running) {
        this->_timer.start(this->_initial_retransmission_timeout);
    }
}
