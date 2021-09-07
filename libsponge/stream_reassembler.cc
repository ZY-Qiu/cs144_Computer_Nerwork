#include "stream_reassembler.hh"

#include <set>
// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity)
    : _unassembled_segment({})
    , _unassembled_byte(0)
    , _eof(false)
    , _pos_eof(0)
    , _output(capacity)
    , _capacity(capacity) {}

// insert into the waitting for assembly set
void StreamReassembler::insert(struct Segment &s) {
    if (_unassembled_segment.size() == 0) {
        _unassembled_segment.insert(s);
        _unassembled_byte += s.data.size();
        return;
    }
    // merge the overlapping segments in the set into a new segment tmp
    struct Segment tmp = s;  // must initialize tmp to s
    size_t start = s.start, size = s.data.size();
    set<struct Segment>::iterator it = _unassembled_segment.lower_bound(s);  // first *it >= s

    // if overlapping with the segment on the left hand side
    if (it != _unassembled_segment.begin()) {
        it--;
        if (start <= it->start + it->data.size()) {           // ecactly filling in the hole also counts, meaning ==
            if (start + size <= it->start + it->data.size())  // included in the left segment
                return;
            tmp.data = it->data + s.data.substr(it->start + it->data.size() - start);
            tmp.start = it->start;
            start = tmp.start;
            size = tmp.data.size();
            _unassembled_byte -= it->data.size();
            _unassembled_segment.erase(it++);
        } else
            it++;  // it still points to the right side
    }
    // if overlapping with the segment on the right hand side
    for (; it != _unassembled_segment.end() &&
           start + size >= it->start;) {  // ecactly filling in the hole also counts, meaning ==
        if (start >= it->start && start + size <= it->start + it->data.size())  // included in the right segment
        {
            return;
        }
        if (start + size < it->start + it->data.size())  // right segment intercept with tmp
        {
            tmp.data += it->data.substr(start + size - it->start);
        }
        // tmp.data now include what is in *it
        _unassembled_byte -= it->data.size();
        _unassembled_segment.erase(it++);
    }
    // insert tmp to the set
    _unassembled_segment.insert(tmp);
    _unassembled_byte += tmp.data.size();
}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.

void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    size_t first_unread = _output.bytes_read();
    size_t first_unassambled = _output.bytes_written();
    size_t first_unaccepted = first_unread + _capacity;
    struct Segment segment(data, index);

    // strange <index + data.size() <= first_unassambled> is wrrong
    // we must deal with empty substring, maybe index, data.size() and first_unassambled are both 0
    // must do eof check
    if (index + data.size() < first_unassambled || first_unaccepted <= index)  // out of bound
        goto JUDGE_EOF;
    if (first_unaccepted < index + data.size())  // out of capacity, truncate
    {
        segment.data = data.substr(0, first_unaccepted - index);
    }
    if (index <= first_unassambled)  // able to write
    {
        _output.write(segment.data.substr(first_unassambled - index));
        // check whether segments in _unassembled_segment can be written
        set<struct Segment>::iterator it = _unassembled_segment.begin();
        while (it->start <= segment.start + segment.data.size()) {
            if (segment.start + segment.data.size() < it->start + it->data.size()) {  // intercept
                _output.write(it->data.substr(segment.start + segment.data.size() - it->start));
            }
            _unassembled_byte -= it->data.size();
            _unassembled_segment.erase(it++);
        }
    } else  // unable to write, insert into the pending set
    {
        insert(segment);
    }

JUDGE_EOF:
    if (eof) {
        _eof = true;
        _pos_eof = index + data.size();
    }
    if (_eof && _output.bytes_written() == _pos_eof) {
        _output.end_input();
    }
}

size_t StreamReassembler::unassembled_bytes() const { return this->_unassembled_byte; }

bool StreamReassembler::empty() const { return this->_unassembled_byte == 0; }