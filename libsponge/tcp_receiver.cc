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
    if(head.syn){
        this->_syn = true;
        this->_isn = head.seqno;
        // add one to point to real data
        seqno = seqno + 1;
        // or end? but have to handle connection on the other direction
        //return;
    }
    uint64_t checkpoint = this->_reassembler.stream_out().bytes_written();
    uint64_t absolute_seqno_64 = unwrap(seqno, this->_isn, checkpoint);
    uint64_t stream_index = absolute_seqno_64 - 1;
    string data = seg.payload().copy();
    this->_reassembler.push_substring(data, stream_index, head.fin);
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if(!this->_syn) return {};
    uint64_t bytes_written = this->stream_out().bytes_written();
    uint64_t absolute_seqno_64 = bytes_written + 1;
    // if fin is set and whole stream **reassembled**, then ByteStream.input_ended() is set, all bytes written
    // then the ackno is the first byte after fin
    if(this->_reassembler.stream_out().input_ended()) 
        return wrap(absolute_seqno_64 + 1, this->_isn);
    else return wrap(absolute_seqno_64, this->_isn);
}

size_t TCPReceiver::window_size() const {
    // bytes_written - bytes_read is the assembled bytes
    return this->_capacity + this->_reassembler.stream_out().bytes_read() - this->_reassembler.stream_out().bytes_written();
}
