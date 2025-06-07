# TCP Sender Bug Analysis: SYN Retransmission Failure

## 问题描述

在 `send_retx.cc` 测试中，TCP sender 无法在超时后重传 SYN 段，导致测试失败。

## 根本原因分析

### 1. 问题位置

在 `/cs144/libsponge/tcp_sender.cc` 的 `fill_window()` 函数中，发送 SYN 段的代码：

```cpp
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
}
```

**关键问题：缺少定时器启动代码！**

### 2. 正确的逻辑应该是什么？

根据 TCP 协议和测试用例的期望：

1. **发送 SYN 段时**：
   - 创建 SYN 段并发送
   - 加入 outstanding_segments 队列
   - **启动重传定时器**
   - 更新状态变量

2. **超时处理 (tick 函数)**：
   - 检查定时器是否超时
   - 如果超时且有未确认的段，重传最早的段
   - 实施指数退避

3. **ACK 处理**：
   - 移除已确认的段
   - 重置定时器和重传计数

### 3. 测试用例期望的流程

`send_retx.cc` 中 "Retx SYN" 测试的期望流程：

```
初始状态 -> fill_window() -> 发送SYN + 启动定时器
     ↓
等待 retx_timeout - 1 ms -> tick() -> 无超时，无重传
     ↓  
等待 1 ms -> tick() -> 超时！-> 重传SYN + 指数退避
     ↓
等待 2*retx_timeout - 1 ms -> tick() -> 无超时
     ↓
等待 1 ms -> tick() -> 超时！-> 重传SYN + 再次指数退避
     ↓
收到 ACK -> ack_received() -> 清除队列，停止重传
```

### 4. 当前实现的问题

1. **SYN 发送时未启动定时器**
   - 结果：定时器处于未运行状态
   - `_timer.check_timeout()` 永远返回 false
   - 即使调用 `tick()`，也不会触发重传

2. **定时器启动位置错误**
   - 只在 `ack_received()` 中启动定时器
   - 但这是在收到 ACK 之后，不是发送段时

3. **重传逻辑缺陷**
   - `tick()` 函数的重传逻辑是正确的
   - 但因为定时器从未启动，条件永远不满足

### 5. 修复方案

在 `fill_window()` 的 SYN 发送部分添加定时器启动：

```cpp
if (_next_seqno == 0) {
    TCPSegment seg;
    seg.header().syn = true;
    seg.header().seqno = wrap(_next_seqno, _isn);
    _segments_out.push(seg);

    // 更新状态
    outstanding_segments.emplace(_next_seqno, move(seg));
    _bytes_in_flight += 1;
    _next_seqno += 1;
    
    // 🔧 修复：启动定时器
    if (!_timer.is_running()) {
        _timer.restart();
    }
}
```

### 6. 类似问题的防范

需要确保在任何发送新段的地方都考虑定时器：

1. **发送 SYN**：启动定时器
2. **发送数据段**：如果定时器未运行，启动定时器  
3. **发送 FIN**：如果定时器未运行，启动定时器

### 7. 测试验证

修复后，测试应该通过以下检查点：

1. 初始 SYN 发送后，定时器开始运行
2. 第一次超时后，重传 SYN，RTO 翻倍
3. 第二次超时后，再次重传 SYN，RTO 再次翻倍
4. 收到 ACK 后，清除队列，重置定时器和重传计数

## 总结

这是一个典型的**定时器管理错误**。发送方在发送需要确认的段时，必须同时启动重传定时器。当前实现遗漏了这个关键步骤，导致重传机制完全失效。

修复这个问题后，TCP sender 应该能够正确实现：
- SYN 重传
- 指数退避
- 超时处理
- ACK 确认

这正是可靠传输协议的核心机制。
