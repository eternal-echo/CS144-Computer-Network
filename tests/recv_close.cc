#include "receiver_harness.hh"
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
            uint32_t isn = uniform_int_distribution<uint32_t>{0, UINT32_MAX}(rd);
            TCPReceiverTestHarness test{4000};
            test.execute(ExpectState{TCPReceiverStateSummary::LISTEN});
            test.execute(SegmentArrives{}.with_syn().with_seqno(isn + 0).with_result(SegmentArrives::Result::OK));
            test.execute(ExpectState{TCPReceiverStateSummary::SYN_RECV});
            test.execute(SegmentArrives{}.with_fin().with_seqno(isn + 1).with_result(SegmentArrives::Result::OK));
            test.execute(ExpectAckno{WrappingInt32{isn + 2}});
            test.execute(ExpectUnassembledBytes{0});
            test.execute(ExpectBytes{""});
            test.execute(ExpectTotalAssembledBytes{0});
            test.execute(ExpectState{TCPReceiverStateSummary::FIN_RECV});
        }

        {
            uint32_t isn = uniform_int_distribution<uint32_t>{0, UINT32_MAX}(rd);
            TCPReceiverTestHarness test{4000};
            // 确认TCP接收器初始状态为LISTEN（等待连接）
            test.execute(ExpectState{TCPReceiverStateSummary::LISTEN});
            // 模拟接收一个带SYN标志的段，序列号是ISN
            //     此时TCP接收器应该:
            //     记录下这个ISN
            //     准备开始接收数据
            test.execute(SegmentArrives{}.with_syn().with_seqno(isn + 0).with_result(SegmentArrives::Result::OK));
            // 确认TCP接收器转换到了SYN_RECV状态（已收到SYN）
            test.execute(ExpectState{TCPReceiverStateSummary::SYN_RECV});
            // 模拟接收一个段，包含:
            // - 序列号为ISN+1（因为SYN占用了一个序列号）
            // - 数据内容为"a"（1个字节）
            // - FIN标志表示流结束
            // TCP接收器应该:
            // - 将"a"添加到重组缓冲区
            // - 因为这个数据是顺序的，应该直接写入输出流
            // - 识别FIN标志并标记流结束
            test.execute(
                SegmentArrives{}.with_fin().with_seqno(isn + 1).with_data("a").with_result(SegmentArrives::Result::OK));
            // 确认TCP接收器转换到了FIN_RECV状态（已收到FIN）
            // 这表明底层ByteStream的end_input()被正确调用
            test.execute(ExpectState{TCPReceiverStateSummary::FIN_RECV});
            // 验证TCP接收器返回的确认号为ISN+3，计算来源:
            //     ISN (SYN) + 1 (数据"a") + 1 (FIN) = ISN+3
            //     表示期望下一个接收的字节序号
            test.execute(ExpectAckno{WrappingInt32{isn + 3}});
            // 确认没有未组装的字节，所有数据已处理
            test.execute(ExpectUnassembledBytes{0});
            // 确认可从输出流读出的字节为"a"
            test.execute(ExpectBytes{"a"});
            // 确认总共组装了1个字节（即"a"）
            test.execute(ExpectTotalAssembledBytes{1});
            // 确认TCP接收器仍然处于FIN_RECV状态
            test.execute(ExpectState{TCPReceiverStateSummary::FIN_RECV});
        }

    } catch (const exception &e) {
        cerr << e.what() << endl;
        return 1;
    }

    return EXIT_SUCCESS;
}
