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

/*
void StreamReassembler::insert(struct Segment &node){
    if (_unassembled_segment.empty()) {
       _unassembled_segment.insert(node);
        _unassembled_byte += node.data.size();
        return;
    }

    struct Segment tmp;
    auto it = _unassembled_segment.lower_bound(node);   // lower_bound返回不小于目标值的第一个对象的迭代器
    size_t x = node.start, sz = node.data.size();
    // 若node的左边有节点，考察是否能与左边的节点合并
    if (it != _unassembled_segment.begin()) {
        it--;   // 定位到左边那个节点
        if (x < it->start + it->data.size() ) {     // 若node与左边相交（相邻不算）或被包含
            if (x + sz <= it->start + it->data.size())      // 若被包含，直接丢弃，否则就是相交
                return;
            tmp.data = it->data + tmp.data.substr(it->start + it->data.size() - x);
            tmp.start = it->start;
            x = tmp.start; sz = tmp.data.size();
            _unassembled_byte -= it->data.size();
            _unassembled_segment.erase(it++);
        } else
            it++;
    }
    // 考察是否能与右边的节点合并，可能与多个节点合并
    while (it != _unassembled_segment.end() && x + sz > it->start) {
        if (x >= it->start && x + sz < it->start + it->data.size()) // 若被右边包含，直接丢弃
            return;
        if (x + sz < it->start + it->data.size()) {     // 若与右边相交
            tmp.data += it->data.substr(x + sz - it->start);
        }
        _unassembled_byte -= it->data.size();   // 相交或包含右边都需要移除节点
        _unassembled_segment.erase(it++);
    }
    _unassembled_segment.insert(tmp);      // tmp是检查合并后的新节点，也有可能没有发生任何合并操作
    _unassembled_byte += tmp.data.size();
}
*/

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
/*
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    struct Segment node{data, index};
    size_t first_unread = _output.bytes_read();
    size_t first_unassembled = _output.bytes_written();
    size_t first_unaccept = first_unread + _capacity;

    if (index + data.size() < first_unassembled || index >= first_unaccept)  // 超出这个范围不作处理
        return;
    if (index + data.size() > first_unaccept)
        node.data = node.data.substr(0, first_unaccept - index); // 若超出capacity，截下要处理的部分

    if (index <= first_unassembled) {   // 若新子串可以直接写入
        _output.write(node.data.substr(first_unassembled - index));
        // 检查缓冲区中的子串能否继续写入
        auto it = _unassembled_segment.begin();
        while (it->start <= _output.bytes_written()) {
            if (it->start + it->data.size() > _output.bytes_written()) // 被包含就不用写入了
                _output.write(it->data.substr(_output.bytes_written() - it->start));
            _unassembled_byte -= it->data.size();
            _unassembled_segment.erase(it++);
        }
    } else {
        insert(node);    // 若不能写入则存入缓冲区
    }

    if (eof) {
        _eof = true;
        _pos_eof = index + data.size();
    }
    if (eof && _output.bytes_written() == _pos_eof)
        _output.end_input();
}*/

size_t StreamReassembler::unassembled_bytes() const { return this->_unassembled_byte; }

bool StreamReassembler::empty() const { return this->_unassembled_byte == 0; }