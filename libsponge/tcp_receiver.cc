#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    TCPHeader head = seg.header();
    WrappingInt32 seqno = head.seqno;
    size_t length;
    if (head.syn) {
        if (this->_syn)
            return;
        this->_syn = true;
        this->_isn = head.seqno;
        this->_recv_base = 1;
        // add one to point to real data
        seqno = seqno + 1;
        // or end? but have to handle connection on the other direction
        length = seg.length_in_sequence_space();
        if (length == 1)
            return;
    } else if (!this->_syn)
        return;

    uint64_t checkpoint = this->_reassembler.stream_out().bytes_written();
    uint64_t absolute_seqno_64 = unwrap(seqno, this->_isn, checkpoint);
    length = seg.length_in_sequence_space();

    if (seg.header().fin) {
        if (this->_fin)
            return;  // refuse other fin if already have one
        this->_fin = true;
    } else {
        // not one-len syn and not fin
        if (length == 0)
            return;
        // check if in the receive window
        if (absolute_seqno_64 >= this->_recv_base + window_size() || absolute_seqno_64 + length <= this->_recv_base)
            return;
    }

    this->_reassembler.push_substring(seg.payload().copy(), absolute_seqno_64 - 1, head.fin);
    this->_recv_base = this->stream_out().bytes_written() + 1;
    if (this->stream_out().input_ended())
        this->_recv_base++;
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (!this->_syn)
        return std::nullopt;
    uint64_t bytes_written = this->stream_out().bytes_written();
    uint64_t absolute_seqno_64 = bytes_written + 1;  // plus one byte for SYN
    // if fin is set and whole stream **reassembled**, then ByteStream.input_ended() is set, all bytes written
    // then the ackno is the first byte after fin
    if (this->_reassembler.stream_out().input_ended())
        return wrap(absolute_seqno_64 + 1, this->_isn);
    else
        return wrap(absolute_seqno_64, this->_isn);
    // return wrap(this->_recv_base, this->_isn);
}

size_t TCPReceiver::window_size() const {
    // bytes_written - bytes_read is the unassembled bytes
    return this->_capacity - this->_reassembler.stream_out().buffer_size();
}
