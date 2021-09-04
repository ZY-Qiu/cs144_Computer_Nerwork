#include "byte_stream.hh"

#include <iostream>
// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity) : _buf(""), _cap(capacity), _eof(false), _written(0), _read(0) {}

// Maybe wrapped by a while loop outside, so do not write more than capacity
size_t ByteStream::write(const string &data) {
    size_t bw = (data.size() > remaining_capacity()) ? remaining_capacity() : data.size();
    // size_t bw = data.size();
    this->_buf += data.substr(0, bw);
    _written += bw;
    return bw;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const { return _buf.substr(0, len); }

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    _buf.erase(0, len);
    _read += len;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    size_t b = (len > buffer_size()) ? buffer_size() : len;
    string r = peek_output(b);
    pop_output(b);
    return r;
}

void ByteStream::end_input() { _eof = true; }

bool ByteStream::input_ended() const { return _eof; }

size_t ByteStream::buffer_size() const { return _buf.size(); }

bool ByteStream::buffer_empty() const { return buffer_size() == 0; }

bool ByteStream::eof() const { return _eof && buffer_empty(); }

size_t ByteStream::bytes_written() const { return _written; }

size_t ByteStream::bytes_read() const { return _read; }

size_t ByteStream::remaining_capacity() const { return (_cap - buffer_size() > 0) ? _cap - buffer_size() : 0; }
