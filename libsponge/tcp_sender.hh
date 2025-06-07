#ifndef SPONGE_LIBSPONGE_TCP_SENDER_HH
#define SPONGE_LIBSPONGE_TCP_SENDER_HH

#include "byte_stream.hh"
#include "tcp_config.hh"
#include "tcp_segment.hh"
#include "wrapping_integers.hh"

#include <functional>
#include <queue>

class Timer {
  private:
    size_t _elapsed = 0;               // 已经过了多少 ms
    size_t _timeout = 0;               // 当前 RTO 超时阈值
    bool _running = false;             // 是否开启

  public:
    Timer() = default;
    Timer(size_t timeout) : _timeout(timeout) {}

    // 启动计时器，清零时间
    void restart() {
        _elapsed = 0;
        _running = true;
    }

    // 停止计时器
    void stop() { _running = false; }

    // 当前是否运行中
    bool is_running() const { return _running; }

    // 设置 RTO
    void set_timeout(size_t timeout) { _timeout = timeout; }

    // 获取当前 RTO
    size_t get_timeout() const { return _timeout; }

    // 时间推进
    void tick(size_t ms) {
        if (_running) {
            _elapsed += ms;
        }
    }

    // 是否已经超时
    bool check_timeout() const {
        return _running && _elapsed >= _timeout;
    }
};


//! \brief The "sender" part of a TCP implementation.

//! Accepts a ByteStream, divides it up into segments and sends the
//! segments, keeps track of which segments are still in-flight,
//! maintains the Retransmission Timer, and retransmits in-flight
//! segments if the retransmission timer expires.
class TCPSender {
  private:
    //! our initial sequence number, the number for our SYN.
    WrappingInt32 _isn;

    //! outbound queue of segments that the TCPSender wants sent
    std::queue<TCPSegment> _segments_out{};

    //! retransmission timer for the connection
    unsigned int _initial_retransmission_timeout;

    //! outgoing stream of bytes that have not yet been sent
    ByteStream _stream;

    //! the (absolute) sequence number for the next byte to be sent
    uint64_t _next_seqno{0};

    //! 已经发送出去但还未收到 ACK 确认的字节数
    size_t _bytes_in_flight = 0;

    //! 是否发送带 SYN/FIN 的包
    bool _fin_flag = false;

    //! 窗口大小，根据文档初始值应为 1
    uint16_t _window_size = 1;

    //! 连续重传次数
    uint32_t _consecutive_retransmissions_count = 0;

    //! 已经发出但还未收到 ACK 确认的 TCPSegment 队列
    std::queue<std::pair<uint64_t, TCPSegment> > outstanding_segments{};

    //! 重传定时器
    Timer _timer;

  public:
    //! Initialize a TCPSender
    TCPSender(const size_t capacity = TCPConfig::DEFAULT_CAPACITY,
              const uint16_t retx_timeout = TCPConfig::TIMEOUT_DFLT,
              const std::optional<WrappingInt32> fixed_isn = {});

    //! \name "Input" interface for the writer
    //!@{
    ByteStream &stream_in() { return _stream; }
    const ByteStream &stream_in() const { return _stream; }
    //!@}

    //! \name Methods that can cause the TCPSender to send a segment
    //!@{

    //! \brief A new acknowledgment was received
    void ack_received(const WrappingInt32 ackno, const uint16_t window_size);

    //! \brief Generate an empty-payload segment (useful for creating empty ACK segments)
    void send_empty_segment();

    //! \brief create and send segments to fill as much of the window as possible
    void fill_window();

    //! \brief Notifies the TCPSender of the passage of time
    void tick(const size_t ms_since_last_tick);
    //!@}

    //! \name Accessors
    //!@{

    //! \brief How many sequence numbers are occupied by segments sent but not yet acknowledged?
    //! \note count is in "sequence space," i.e. SYN and FIN each count for one byte
    //! (see TCPSegment::length_in_sequence_space())
    size_t bytes_in_flight() const;

    //! \brief Number of consecutive retransmissions that have occurred in a row
    unsigned int consecutive_retransmissions() const;

    //! \brief TCPSegments that the TCPSender has enqueued for transmission.
    //! \note These must be dequeued and sent by the TCPConnection,
    //! which will need to fill in the fields that are set by the TCPReceiver
    //! (ackno and window size) before sending.
    std::queue<TCPSegment> &segments_out() { return _segments_out; }
    //!@}

    //! \name What is the next sequence number? (used for testing)
    //!@{

    //! \brief absolute seqno for the next byte to be sent
    uint64_t next_seqno_absolute() const { return _next_seqno; }

    //! \brief relative seqno for the next byte to be sent
    WrappingInt32 next_seqno() const { return wrap(_next_seqno, _isn); }
    //!@}
};

#endif  // SPONGE_LIBSPONGE_TCP_SENDER_HH
