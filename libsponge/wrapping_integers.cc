#include "wrapping_integers.hh"

// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    // do carry to wrap around 32-bit
    n = n + isn.raw_value();
    uint32_t tmp = (n << 32) >> 32;
    return WrappingInt32(tmp);
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    uint64_t absolute_seqno_64 = static_cast<uint32_t>(n - isn);
    // unsigned operation, now 32-bit absolute value
    if (checkpoint <= absolute_seqno_64)
        return absolute_seqno_64;
    else {
        // find the number closet to the checkpoint in +-2^32
        uint64_t size_period = 1ul << 32;
        absolute_seqno_64 += (checkpoint & 0xffffffff00000000);
        uint64_t diff = max(checkpoint, absolute_seqno_64) - min(checkpoint, absolute_seqno_64);
        if (diff <= size_period / 2)
            return absolute_seqno_64;
        else {
            if (checkpoint <= absolute_seqno_64)
                if (absolute_seqno_64 >= (1ul << 32))
                    return absolute_seqno_64 - (1ul << 32);
                else
                    return absolute_seqno_64;
            else
                return absolute_seqno_64 + (1ul << 32);
        }
    }
}
