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
    return WrappingInt32{static_cast<uint32_t>(n + isn.raw_value())};
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number 表示 the index ofthe last reassembled byte，而 x 取与 checkpoint 最接近的值
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    // 其实 x 的低 32 bit 已经确定，而 checkpoint±2^31至多使高 32 bit “+1” 或 “-1”，并且不会同时发生。因此只需要分两种情况，拼接 x 的低 32 bit 和 checkpoint ± 2^31的高 32 bit，取最接近 checkpoint 的值即是答案。
    
    // 获取 n 相对于 isn 的 32 位偏移
    uint32_t offset = n - isn;

    uint64_t candidate_plus = ((checkpoint + (1ULL << 31)) & 0xFFFFFFFF00000000ULL) | offset;
    uint64_t candidate_minus = ((checkpoint - (1ULL << 31)) & 0xFFFFFFFF00000000ULL) | offset;

    // 计算距离
    uint64_t distance_plus = candidate_plus > checkpoint ? candidate_plus - checkpoint : checkpoint - candidate_plus;
    uint64_t distance_minus = candidate_minus > checkpoint ? candidate_minus - checkpoint : checkpoint - candidate_minus;

    return distance_plus < distance_minus ? candidate_plus : candidate_minus;
}