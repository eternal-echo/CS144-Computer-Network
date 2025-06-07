#include "sender_harness.hh"
#include "wrapping_integers.hh"

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

using namespace std;

int main() {
    try {
        auto rd = get_random_generator();

        {
            TCPConfig cfg;
            WrappingInt32 isn(rd());
            cfg.fixed_isn = isn;

            TCPSenderTestHarness test{"SYN sent test", cfg};
            // 这一步验证TCP发送端的初始状态是SYN_SENT。当TCP发送端被创建时，它应该自动尝试建立连接，发送一个SYN包，并进入SYN_SENT状态。
            test.execute(ExpectState{TCPSenderStateSummary::SYN_SENT});
            // 这一步验证TCP发送端确实发送了一个正确的SYN段：
            // - 标志位只有SYN被设置
            // - 没有携带数据(payload_size=0)
            // - 序列号是预设的ISN
            test.execute(ExpectSegment{}.with_no_flags().with_syn(true).with_payload_size(0).with_seqno(isn));
            // 最后验证TCP发送端正确跟踪了"飞行中"(即已发送但未收到确认)的字节数。SYN标志占用一个序列号，因此bytes_in_flight应该是1。
            test.execute(ExpectBytesInFlight{1});

            // 为了通过这个测试，你的TCP发送端实现需要：
            // 在构造函数中：
            // - 保存ISN
            // - 初始化计时器、字节流等
            // - 调用fill_window()方法开始连接建立
            // 在fill_window()方法中：
            // - 如果_next_seqno是0，说明还没有发送过SYN
            // - 创建一个只有SYN标志的段
            // - 设置序列号为ISN
            // - 将段加入发送队列
            // - 更新_next_seqno为1
            // - 可能需要启动重传计时器
            // 在bytes_in_flight()方法中：
            // - 正确计算未确认字节数，包括SYN和FIN占用的序列号
        }

        {
            TCPConfig cfg;
            WrappingInt32 isn(rd());
            cfg.fixed_isn = isn;
            
            // 初始化与发送SYN
            // - TCP客户端在开始连接时选择一个初始序列号(ISN)
            // - 发送一个SYN包，序列号为ISN
            // - 客户端状态从CLOSED转为SYN_SENT
            // - 这个阶段对应三次握手的第一步
            TCPSenderTestHarness test{"SYN acked test", cfg};
            test.execute(ExpectState{TCPSenderStateSummary::SYN_SENT});
            // 验证SYN段内容
            // - SYN段只设置SYN标志位，无其他标志
            // - SYN段不携带数据payload
            // - SYN段的序列号就是ISN
            // - SYN段在序列号空间中占用1个字节，因此有1个字节"在飞行中"
            // - TCP必须跟踪未确认的字节数，以支持重传机制
            test.execute(ExpectSegment{}.with_no_flags().with_syn(true).with_payload_size(0).with_seqno(isn));
            test.execute(ExpectBytesInFlight{1});
            // 接收ACK并状态转换
            // - 收到确认号为ISN+1的ACK，表示接收方已收到SYN
            // - 确认号的值表示接收方期望收到的下一个字节序号
            // - TCP发送端状态从SYN_SENT转为SYN_ACKED（这是标准TCP状态机中ESTABLISHED的前置状态）
            // - 这对应三次握手的第二步完成
            test.execute(AckReceived{WrappingInt32{isn + 1}});
            test.execute(ExpectState{TCPSenderStateSummary::SYN_ACKED});
            // 验证收到ACK后的行为
            // - 收到ACK后不应自动发送新段（因为还没有应用数据要发送）
            // - 由于SYN已被确认，飞行中的字节数应减为0
            // - TCP协议设计中，确认(ACK)不占用序列号空间
            test.execute(ExpectNoSegment{});
            test.execute(ExpectBytesInFlight{0});
        }

        {
            TCPConfig cfg;
            WrappingInt32 isn(rd());
            cfg.fixed_isn = isn;

            TCPSenderTestHarness test{"SYN -> wrong ack test", cfg};
            test.execute(ExpectState{TCPSenderStateSummary::SYN_SENT});
            test.execute(ExpectSegment{}.with_no_flags().with_syn(true).with_payload_size(0).with_seqno(isn));
            test.execute(ExpectBytesInFlight{1});
            test.execute(AckReceived{WrappingInt32{isn}});
            test.execute(ExpectState{TCPSenderStateSummary::SYN_SENT});
            test.execute(ExpectNoSegment{});
            test.execute(ExpectBytesInFlight{1});
        }

        {
            TCPConfig cfg;
            WrappingInt32 isn(rd());
            cfg.fixed_isn = isn;

            // 连接建立阶段（与前一个测试用例相同）
            // - 这部分与前面测试用例相同，完成了TCP三次握手的前两步
            // - 发送SYN(第一次握手)并接收ACK(第二次握手的一部分)
            // - 状态从SYN_SENT转为SYN_ACKED，表示连接已部分建立
            TCPSenderTestHarness test{"SYN acked, data", cfg};
            test.execute(ExpectState{TCPSenderStateSummary::SYN_SENT});
            test.execute(ExpectSegment{}.with_no_flags().with_syn(true).with_payload_size(0).with_seqno(isn));
            test.execute(ExpectBytesInFlight{1});
            test.execute(AckReceived{WrappingInt32{isn + 1}});
            test.execute(ExpectState{TCPSenderStateSummary::SYN_ACKED});
            // 应用层写入数据
            test.execute(ExpectNoSegment{});
            // - 应用层通过write()系统调用向TCP发送数据"abcdefgh"
            // - 数据被写入TCP的发送缓冲区(内部ByteStream)
            test.execute(ExpectBytesInFlight{0});
            test.execute(WriteBytes{"abcdefgh"});
            // - Tick{1}模拟时间流逝1毫秒，触发TCP检查是否有数据需要发送
            test.execute(Tick{1});
            // - TCP使用计时器驱动，定期检查状态并执行必要操作
            // 数据段发送
            // - TCP状态保持在SYN_ACKED，因为连接建立后数据传输不改变连接状态
            // - 发送一个数据段，包含应用层写入的8个字节
            // - 数据段的序列号是isn + 1，因为SYN占用了序列号空间中的1个位置
            // - 序列号设计保证了数据有序性：
            //   - SYN的序列号是ISN
            //   - 第一个数据字节的序列号是ISN+1
            //   - 后续字节依次递增
            // - 跟踪"飞行中"的字节数增加了8个，表示有8个数据字节已发送但未收到确认
            test.execute(ExpectState{TCPSenderStateSummary::SYN_ACKED});
            test.execute(ExpectSegment{}.with_seqno(isn + 1).with_data("abcdefgh"));
            test.execute(ExpectBytesInFlight{8});
            test.execute(AckReceived{WrappingInt32{isn + 9}});
            test.execute(ExpectState{TCPSenderStateSummary::SYN_ACKED});
            test.execute(ExpectNoSegment{});
            test.execute(ExpectBytesInFlight{0});
            test.execute(ExpectSeqno{WrappingInt32{isn + 9}});
        }

    } catch (const exception &e) {
        cerr << e.what() << endl;
        return 1;
    }

    return EXIT_SUCCESS;
}
