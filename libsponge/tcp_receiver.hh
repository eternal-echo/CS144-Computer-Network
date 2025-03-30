#ifndef SPONGE_LIBSPONGE_TCP_RECEIVER_HH
#define SPONGE_LIBSPONGE_TCP_RECEIVER_HH

#include "byte_stream.hh"
#include "stream_reassembler.hh"
#include "tcp_segment.hh"
#include "wrapping_integers.hh"

#include <optional>

//! \brief The "receiver" part of a TCP implementation.

//! Receives and reassembles segments into a ByteStream, and computes
//! the acknowledgment number and window size to advertise back to the
//! remote TCPSender.
class TCPReceiver {
    //! Our data structure for re-assembling bytes.
    StreamReassembler _reassembler;

    //! The maximum number of bytes we'll store.
    size_t _capacity;

    //! The initial sequence number of the sender.
    std::optional<WrappingInt32> _isn;

  public:
    //! \brief Construct a TCP receiver
    //!
    //! \param capacity the maximum number of bytes that the receiver will
    //!                 store in its buffers at any give time.
    TCPReceiver(const size_t capacity) : _reassembler(capacity), _capacity(capacity), _isn(std::nullopt) {}

    //! \name Accessors to provide feedback to the remote TCPSender
    //!@{

    // 这个是计算出“我已经收到什么字节了，现在想要对方发什么？”
    // 需求：
    //     如果还没收到 SYN，返回 nullopt（连接未建立）
    //     否则返回“下一个我希望收到的字节的序号”
    //     要考虑：
    //         SYN 占一个 seqno
    //         如果已经收到了 FIN，FIN 也占一个 seqno
    // 💡 基础知识连接：
    //     ackno = wrap(已重组字节数 + SYN + FIN, ISN)
    //     TCP 是累积确认（cumulative ACK）
    //! \brief The ackno that should be sent to the peer
    //! \returns empty if no SYN has been received
    //!
    //! This is the beginning of the receiver's window, or in other words, the sequence number
    //! of the first byte in the stream that the receiver hasn't received.
    std::optional<WrappingInt32> ackno() const;

    //! \brief The window size that should be sent to the peer
    //!
    //! Operationally: the capacity minus the number of bytes that the
    //! TCPReceiver is holding in its byte stream (those that have been
    //! reassembled, but not consumed).
    //!
    //! Formally: the difference between (a) the sequence number of
    //! the first byte that falls after the window (and will not be
    //! accepted by the receiver) and (b) the sequence number of the
    //! beginning of the window (the ackno).
    size_t window_size() const;
    //!@}

    // 有时数据乱序到达，不能立即拼接
    // 这函数让你知道还有多少字节在 buffer 里但还没拼好
    //! \brief number of bytes stored but not yet reassembled
    size_t unassembled_bytes() const { return _reassembler.unassembled_bytes(); }

    //! \brief handle an inbound segment
    // 这个是核心函数，每收到一个 TCP segment 都要调用它处理。
    // 需求：
    //     如果 segment 是 SYN，提取 ISN
    //     如果没收到 SYN 就不接受数据（流未开始）
    //     把 segment 中的 payload 数据按 seqno → 转换成 stream index
    //     调用 _reassembler.push_substring() 来重组字节
    //     如果 segment 是 FIN，要记录下来（结束标志）
    // 💡背后基础知识：
    //     TCP段中的 seqno 是 32位，需要 unwrap 成 64位再算出 stream_index
    //     SYN 和 FIN 各占一个 seqno 空间，但不算进流内容（stream index）
    void segment_received(const TCPSegment &seg);

    //! \name "Output" interface for the reader
    //!@{
    ByteStream &stream_out() { return _reassembler.stream_out(); }
    const ByteStream &stream_out() const { return _reassembler.stream_out(); }
    //!@}
};

#endif  // SPONGE_LIBSPONGE_TCP_RECEIVER_HH
