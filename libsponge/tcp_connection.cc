#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return this->_sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return this->_sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return this->_receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return this->_time_since_last_segment_received; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    if (!this->_active)
        return;
    this->_time_since_last_segment_received = 0;
    // ack received in closed state is meaningless
    if (seg.header().ack && (s_state() == closed))
        return;
    // if rst flag is set
    if (seg.header().rst) {
        // receive rst before connection established
        // if (s_state() < syn_acked)
        //    return;
        unclean_shutdown(false);
        return;
    }
    // give it to the TCPReceiver
    this->_receiver.segment_received(seg);
    // if ack flag is set, give it to TCPSender
    if (seg.header().ack) {
        this->_sender.ack_received(seg.header().ackno, seg.header().win);
    }
    // initiate connection if not
    if (seg.header().syn && (r_state() == listening) && (s_state() == closed)) {
        connect();
        return;
    }
    // if consume seqno, at least one segment will be sent back
    // segmenet should has length, syn or fin or has payload
    
    if (seg.length_in_sequence_space() > 0 && (r_state() != listening) && this->_sender.segments_out().empty()) {
        // if receive syn at very beginning, should not send empty segment here
        // want to send syn + ack back
        if((seg.header().syn && !seg.header().ack) && s_state() == closed) {}
        else
            this->_sender.send_empty_segment();
    }
    push_segment_out();
}

bool TCPConnection::active() const { return this->_active; }

size_t TCPConnection::write(const string &data) {
    size_t bw = this->_sender.stream_in().write(data);
    push_segment_out();
    return bw;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    if (!this->_active)
        return;
    this->_time_since_last_segment_received += ms_since_last_tick;
    this->_sender.tick(ms_since_last_tick);
    // if retransmit time reach the limit, close the connection
    if (this->_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        unclean_shutdown(true);
    }
    push_segment_out(); // end the connection cleanly if possible, important
}

void TCPConnection::end_input_stream() {
    // set the _eof flag in the sender bytestream, then send
    this->_sender.stream_in().end_input();
    push_segment_out();
}

void TCPConnection::connect() {
    // now the sender bytestream is empty and _syn flag not set, cause SYN sent
    push_segment_out(true);
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            unclean_shutdown(true);
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

// send the bytes stored in the sender bytestream
void TCPConnection::push_segment_out(bool send_syn) {
    // operate on _sender._segments_out, if _send_rst, then send rst
    // if already send syn or will send syn or just receive syn
    if(send_syn || (s_state() != closed) || (r_state() != listening)) { 
        this->_sender.fill_window();  // set up this->_sender.segments_out(), will send syn if not SYN-ed yet
    }
    TCPSegment seg;
    while (!this->_sender.segments_out().empty()) {
        // to send, we need only to add the receiver info into the segment, for sender already done its info
        seg = this->_sender.segments_out().front();
        this->_sender.segments_out().pop();
        if (r_state() != listening) {  // if receiver is not in listening state, where waiting for SYN
            // only syn packet do not have ack
            seg.header().ack = true;
            seg.header().ackno = this->_receiver.ackno().value();
            seg.header().win =
                min(this->_receiver.window_size(), static_cast<size_t>(numeric_limits<uint16_t>().max()));
        }
        if (this->_send_rst) {
            this->_send_rst = false;
            seg.header().rst = true;
        }
        // push seg to the connection's own queue, which will be dequeue by the os
        this->segments_out().push(seg);
    }
    // check is implemented inside of clean_shutdown()
    clean_shutdown();
}

void TCPConnection::clean_shutdown() {
    // if the TCPConnection’s inbound stream ends before the TCPConnection has ever sent a fin segment,
    // then the TCPConnection doesn’t need to linger after both streams finish
    if ((r_state() == fin_recv) && (s_state() < fin_sent)) {
        this->_linger_after_streams_finish = false;
    }
    // try to deactivate the connection if 1. inbound assembled and ended, 2. outbound fully sent with fin
    //                                     3. outbound fully acknowledged by remote peer
    if ((r_state() == fin_recv) && (s_state() == fin_acked)) {
        // set the _active to false if not linger or time_wait pass
        if (!this->_linger_after_streams_finish || (time_since_last_segment_received() >= 10 * this->_cfg.rt_timeout)) {
            this->_active = false;
        }
    }
}

// set error state, send rst......
void TCPConnection::unclean_shutdown(bool send_rst) {
    // set the error state in both sender and receiver
    this->_sender.stream_in().set_error();
    this->_receiver.stream_out().set_error();
    // active() = false immediately
    this->_active = false;
    // may by sender or receiver of rst, send_rst == true means you should send out rst info
    if (send_rst) {
        this->_send_rst = true;
        if (this->_sender.segments_out().empty()) {
            // send a zero length packet only to alarm of rst
            this->_sender.send_empty_segment();
        }
        push_segment_out();
    }
}

receiver_state TCPConnection::r_state() {
    if (!this->_receiver.ackno().has_value())
        return listening;
    else if (!this->_receiver.stream_out().input_ended())
        return syn_received;
    else if (this->_receiver.stream_out().input_ended())
        return fin_recv;
    return unk;
}

sender_state TCPConnection::s_state() {
    if (this->_sender.next_seqno_absolute() == 0)
        return closed;
    else if (this->_sender.next_seqno_absolute() == this->bytes_in_flight())
        return syn_sent;
    if ((this->_sender.next_seqno_absolute() > this->bytes_in_flight() && !this->_sender.stream_in().eof()) ||
        (this->_sender.stream_in().eof() &&
         (this->_sender.next_seqno_absolute() < this->_sender.stream_in().bytes_written() + 2)))
        return syn_acked;
    if (this->_sender.stream_in().eof() &&
        (this->_sender.next_seqno_absolute() == this->_sender.stream_in().bytes_written() + 2)) {
        if (this->bytes_in_flight() > 0)
            return fin_sent;
        else
            return fin_acked;
    }
    return unknown;
}