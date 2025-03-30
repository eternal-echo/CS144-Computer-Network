#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

/**
 *  \brief 当前 TCPReceiver 大体上有三种状态， 分别是
 *      1. LISTEN，此时 SYN 包尚未抵达。可以通过 _set_syn_flag 标志位来判断是否在当前状态
 *      2. SYN_RECV, 此时 SYN 抵达。只能判断当前不在 1、3状态时才能确定在当前状态
 *      3. FIN_RECV, 此时 FIN 抵达。可以通过 ByteStream end_input 来判断是否在当前状态
 */
void TCPReceiver::segment_received(const TCPSegment &seg) {
    const auto &header = seg.header();

    /* 🪜 Step 1：判断是不是第一次 SYN */
    //  TCP 建立连接靠三次握手，第一个段一定有 SYN。
    // 如果还没收到 SYN，不接受数据
    if (!_isn.has_value()) {
        /* 🛑 Step 2：没收到 SYN 的段都忽略 */
        // TCP连接必须先建立，不能提前传数据。
        if (!header.syn) {
            return; // 没有 ISN 就无法计算位置，也不能处理数据。所以直接忽略掉这个 segment（保护逻辑）
        }
        _isn = header.seqno; // 建立连接的 seqno 即是 ISN，也是 SYN 对应的 seqno
    }

    // 🧮 Step 3：算出这个 segment 数据对应流中的位置（stream index）
    // TCP 把字节当成流来传，每个字节有一个序号。接收方要根据 segment 的 seqno 算出：
    //  “这个数据的第一个字节是流的第几个字节？”
    uint64_t checkpoint = _reassembler.stream_out().bytes_written();
    uint64_t abs_seqno = unwrap(header.seqno, _isn.value(), checkpoint); // ACK编号 发送方判断哪些字节已确认 会回绕
    uint64_t stream_index = header.syn ? 0 : abs_seqno - 1; // ✳️ 为什么减 1？因为 ISN 编号的是 SYN，不是流中数据。所以第一个数据字节 = ISN + 1，对应 stream index = 0
    
    /* 🧩 Step 4：把数据送进 StreamReassembler */
    // TCP 数据可能乱序到达，要放进 buffer，按位置拼接。
    // 交给 Reassembler：
    // - 数据内容
    // - 在流中的位置（stream index）
    // - 是否包含 FIN（流结束）
    _reassembler.push_substring(seg.payload().copy(), stream_index, header.fin);
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    // 如果还没收到 SYN，返回 nullopt（连接未建立）
    if (!_isn.has_value()) {
        return nullopt;
    }
    // 否则返回“下一个我希望收到的字节的序号”
    uint64_t ackno = _reassembler.stream_out().bytes_written() + 1 + 
        ((_reassembler.stream_out().input_ended()) ? 1 : 0);
    return wrap(ackno, _isn.value());
}

size_t TCPReceiver::window_size() const {
    // 容量减去已经重组的字节流大小=剩余窗口大小
    return _capacity - _reassembler.stream_out().buffer_size();
}
