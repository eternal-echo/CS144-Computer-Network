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

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

/* 
根据接收方的窗口，尽可能从 ByteStream 中读取数据，封装成 TCP 报文段（segment），加上正确的 seqno（SYN/FIN），放入 _segments_out，准备发送。
例子：
- 接收方说：“我还能收 10 字节”
- 发送方要：
    - 看窗口剩多少
    - 从 ByteStream 中读数据
    - 创建 TCP 段，每段最大 1452 字节
    - 加上 seqno（绝对 seqno → wrap）
    - 发出去（放进 _segments_out）
✅ 基本逻辑：
    如果还没发过 SYN，先发 SYN；
    - 一直发数据直到窗口满了或流没了；
    - 如果数据发完了，并且 ByteStream eof()，再发送 FIN；
👉 每次生成一个段，都要更新：
    - next_seqno（绝对序号）
    - bytes_in_flight
    - 加入 _segments_out 发送队列
    - 加入 outstanding_segments（你需要定义一个队列或列表）
📌 注意事项：
    每个段最大 1452 字节（由 TCPConfig::MAX_PAYLOAD_SIZE 限制）
    SYN / FIN 各占一个 seqno
    窗口为 0 时按 1 处理（zero window probing） 
    */
void TCPSender::fill_window() {
    // ✅ 第一步：发送 SYN（连接建立）
    if (_next_seqno == 0) {
        TCPSegment seg;
        seg.header().syn = true;
        seg.header().seqno = wrap(_next_seqno, _isn);
        _segments_out.push(seg);
    
        // 更新状态
        outstanding_segments.emplace(_next_seqno, move(seg)); // 维护的重传队列
        _bytes_in_flight += 1;
        _next_seqno += 1;

        if (!_timer.is_running()) {
            _timer.restart();
        }
    }
    
    // ✅ 第二步：主循环发数据（填满窗口） 
    // TCP 原理： 接收方窗口告诉我还能发多少，我就从 ByteStream 抓数据封装发送。
    size_t space_left = _window_size == 0 ? 1 : _window_size; // 窗口为 0 时按 1 处理（zero window probing）
    
    // 防止整数下溢：如果已发送字节数超过窗口大小，则没有剩余空间
    size_t window_remain = 0;
    if (space_left > bytes_in_flight()) {
        window_remain = space_left - bytes_in_flight(); // 窗口剩余空间
    }
    
    while (window_remain > 0) { // 有窗口 && 有字节可发送？ 
        // [从 ByteStream 读数据 ≤ min(窗口, 1452)]
        size_t max_payload = min(TCPConfig::MAX_PAYLOAD_SIZE, window_remain);
        string payload = _stream.read(max_payload);

        // 如果没有数据，检查是否需要发送单独的FIN段
        if (payload.empty()) {
            // 只有在真正没有更多数据可发送时才考虑FIN
            // 条件：stream已结束 && buffer为空 && 还没发过FIN && 有足够窗口
            if (!_fin_flag && _stream.eof() && _stream.buffer_empty() && window_remain >= 1) {
                TCPSegment fin_seg;
                fin_seg.header().seqno = next_seqno();
                fin_seg.header().fin = true;
                _fin_flag = true;

                _segments_out.push(fin_seg);
                outstanding_segments.emplace(_next_seqno, move(fin_seg));

                // 启动定时器（如果尚未运行）
                if (!_timer.is_running()) {
                    _timer.restart();
                }

                _next_seqno += 1;  // FIN 占用1个序号
                _bytes_in_flight += 1;
                window_remain -= 1;
            }
            break;
        }

        // [构造段，加payload，包seqno]
        TCPSegment seg;
        seg.payload() = Buffer(std::move(payload));  // 把ByteStream的数据放进段里
        seg.header().seqno = next_seqno(); // next_seqno() 是 TCP seqno
        size_t seq_len = seg.length_in_sequence_space(); // seq_len就是 payload 长度，需要更新

        
        // ✅ 第三步：发送 FIN（数据发送完了，主动关闭）
        if (!_fin_flag && _stream.eof() && _stream.buffer_empty() &&
            seq_len + 1 <= window_remain) {
            seg.header().fin = true;
            _fin_flag = true;
            seq_len += 1;  // FIN 也占 1 字节
        }

        // [推入segments_out & outstanding]
        _segments_out.push(seg);
        outstanding_segments.emplace(_next_seqno, move(seg)); // 维护的重传队列

        // 启动定时器（如果尚未运行）
        if (!_timer.is_running()) {
            _timer.restart();
        }

        // [更新 next_seqno, in_flight]
        _next_seqno += seq_len;
        _bytes_in_flight += seq_len;
        window_remain -= seq_len;
    }
}

// 功能：
//  - 判断 ack 是否有效？
//  - 清除已经确认的 segment
//     将已被 ACK 的段从 outstanding_segments 移除；
//     更新 bytes_in_flight；
//  - 更新窗口大小；
//     重启定时器；
//     调用 fill_window() 看有没有新窗口可用；
//     如果 ackno 有推进 → 重传次数清零，RTO 还原。
// 📌 注意：
//     要用 unwrap() 把 ack 转成绝对位置；
//     只有 ackno > segment_end 才表示该段被完全确认；
//     如果是重复 ACK，不清零计时器。
//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    // 1. 计算绝对 ackno
    uint64_t abs_ackno = unwrap(ackno, _isn, _next_seqno);
    if (abs_ackno > next_seqno_absolute()) return; // 传入的 ACK 是不可靠的，直接丢弃
    // 2. 更新窗口大小
    _window_size = window_size; 
    // 3. 检查 outstanding_segments，删除已确认的段
    bool acked = false;
    while (!outstanding_segments.empty()) {
        auto &[abs_seqno, seg] = outstanding_segments.front(); // 取出队首段
        uint64_t abs_seg_end = abs_seqno + seg.length_in_sequence_space(); // 段的结束位置
        if (abs_seg_end <= abs_ackno) { // 如果这个段已经被确认
            _bytes_in_flight -= seg.length_in_sequence_space(); // 更新已发送但未确认的字节数
            outstanding_segments.pop(); // 移除已确认的段
            acked = true; // 只要有一个段被确认，就算 ACK 有效
        } else {
            break; // 只要有一个没确认，就不处理后面的了
        }
    }
    // 如果有 ack 成功，处理副作用
    if (acked) {
        _consecutive_retransmissions_count = 0;
        _timer.set_timeout(_initial_retransmission_timeout);
        _timer.restart();
    }

    if (_bytes_in_flight == 0) {
        // _timer.stop();
    }
    
    // 重启定时器
    fill_window(); // 看看有没有新窗口可用
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    _timer.tick(ms_since_last_tick);

    if (_timer.check_timeout() && !outstanding_segments.empty()) {
        // 重传最早未确认段
        const TCPSegment &seg = outstanding_segments.front().second;
        _segments_out.push(seg);  // 重发

        if (_window_size > 0) {
            _consecutive_retransmissions_count++;
            _timer.set_timeout(_timer.get_timeout() * 2);  // 指数回退
        }

        _timer.restart();
    }
}

unsigned int TCPSender::consecutive_retransmissions() const {
    return _consecutive_retransmissions_count;
}

void TCPSender::send_empty_segment() {
    TCPSegment seg;
    seg.header().seqno = next_seqno(); // 就是当前还没发出的 seqno
    _segments_out.push(seg);
}
