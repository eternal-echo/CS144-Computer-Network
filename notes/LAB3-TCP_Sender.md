![1000240459.png](attachment:2cff2260-ab7f-4304-9f25-ab94c23ffed6:1000240459.png)

- 功能：
    - 【传输字节流】将 ByteStream 中的数据以 TCP 报文形式**持续**发送给接收者。
    - 【处理ackno和窗口大小】处理 TCPReceiver 传入的 ackno 和 window size，以追踪接收者当前的接收状态，以及检测丢包情况。
    - 【超时重传】若**经过一个超时时间后**仍然**没有接收到 TCPReceiver 发送的针对某个数据包的 ack 包**，则重传对应的原始数据包。
    - 【自动重传】ARQ（Automatic Repeat Request）原则：发送接收方允许我们发送的**任何内容（填充窗口，可以有重复）**，并不断**重传**，直到接收方确认**每段内容**。发送方的工作是确保接收方至少获得**每个字节一次**
- 接口：
    - **fill_window**：TCPSender 从 ByteStream 中读取数据，并以 TCPSegement 的形式发送，尽可能地填充接收者的**窗口**。但每个TCP段的大小不得超过 `TCPConfig::MAX PAYLOAD SIZE`。
        
        > 若接收方的 Windows size 为 0，则发送方将按照接收方 window size 为 1 的情况进行处理，持续发包。
        > 
        > 
        > 因为虽然此时发送方发送的数据包可能会被接收方拒绝，但接收方可以在反向发送 ack 包时，将自己最新的 window size 返回给发送者。否则若双方停止了通信，那么当接收方的 window size 变大后，发送方仍然无法得知接收方可接受的字节数量。
        > 
        > 若远程没有 ack 这个在 window size 为 0 的情况下发送的一字节数据包，那么发送者重传时**不要将 RTO 乘2**。这是因为将 RTO 双倍的目的是为了避免网络拥堵，但此时的数据包丢弃并不是因为网络拥堵的问题，而是远程放不下了。
        > 
    - **ack_received**：对接收方返回的 ackno 和 window size 进行处理。丢弃那些**已经完全确认但仍然处于追踪队列**的数据包。同时如果 window size 仍然存在空闲，则继续发包。
    - **tick**：该函数将会被调用以指示经过的时间长度。发送方可能需要重新发送一些超时且没有被确认的数据包。
    - **send_empty_segment**：生成并发送一个**在 seq 空间中长度为 0** 并**正确设置 seqno** 的 TCPSegment，这可让用户发送一个空的 ACK 段。
- TCPSender 的状态图：
    - /lib_sponge/tcp_helper/tcp_state.cc
    
    ![image.png](attachment:55e4f6eb-bfad-4980-9bcb-2d5b44800c73:image.png)
    
    - TCP 状态流转详解
        1. TCP 连接建立流程（三次握手）
            1. 初始状态：
                - 客户端：CLOSED
                - 服务端：LISTEN
            2. 状态转换过程：
                
                ```
                Client (CLOSED)                 Server (LISTEN)
                    |                               |
                    | -------- SYN seq=x --------> |
                    | (fill_window发送SYN)         | (状态转为SYN_RCVD)
                    |                              |
                    | <---- SYN+ACK seq=y,ack=x+1--|
                    | (ack_received处理)           |
                    |                              |
                    | -------- ACK ack=y+1 ------> |
                    | (状态转为ESTABLISHED)        | (状态转为ESTABLISHED)
                
                ```
                
        2. 数据传输阶段的工作流程
            1. **发送数据的触发时机**：
                - 应用层写入数据到 ByteStream
                - fill_window() 被调用
                - 收到新的 ACK 确认（可能打开新的窗口空间）
            2. **数据发送流程**：
                
                ```
                应用层数据 -> ByteStream -> fill_window()
                -> 创建 TCPSegment -> 设置序号
                -> 加入 segments_out & outstanding_segments
                
                ```
                
            3. **数据确认流程**：
                
                ```
                收到ACK -> ack_received() -> 移除已确认段
                -> 重置重传计时器 -> 可能触发新的 fill_window()
                
                ```
                
        3. 重传机制详解
            1. **定时器工作方式**：
                - tick() 定期被调用，更新计时器
                - 超时时，重传最早的未确认段
                - 窗口非0时，超时间隔翻倍（拥塞控制）
            2. **重传触发条件**：
                
                ```
                定时器超时 && outstanding_segments非空
                -> 重传队首段
                -> 如果窗口>0：重传次数+1，超时时间×2
                -> 重启定时器
                
                ```
                
        4. 常见测试场景状态流转
            1. **基本数据传输测试**：
                
                ```
                CLOSED -> SYN_SENT（发送SYN）
                      -> ESTABLISHED（收到SYN+ACK，发送数据）
                      -> 持续发送数据并处理ACK
                
                ```
                
            2. **丢包重传测试**：
                
                ```
                发送数据段 -> 等待ACK超时
                -> 重传数据段 -> 收到ACK
                -> 继续发送新数据
                
                ```
                
            3. **零窗口测试**：
                
                ```
                正常发送 -> 收到窗口=0的ACK
                -> 发送1字节探测包
                -> 等待窗口更新
                
                ```
                
        5. 关键指标和限制
            1. **性能指标**：
                - 重传超时时间(RTO)：起始值可配置
                - 最大报文段大小：TCPConfig::MAX_PAYLOAD_SIZE (1452字节)
                - 重传次数：由consecutive_retransmissions()跟踪
            2. **资源限制**：
                - 发送缓冲区：由ByteStream容量限制
                - 飞行中字节数：由接收方窗口限制
    - **TCP完整的发送流程：流控+序列号管理+重传**
        
        发送方的复杂性主要来源于 **可靠传输** 和 **流控制** 这两个因素：
        
        1. **流控制**：
            - 发送方要根据接收方的 **窗口大小**，决定发送多少数据。接收方的 **`window_size`** 会影响发送方的 **`fill_window()`** 行为。
            - 发送方不仅要知道接收方当前窗口有多大，还要时刻判断窗口是否有空闲空间，并动态填充数据。
        2. **序列号管理**：
            - 发送方需要精确管理每个 **字节的序列号**。它需要对每个字节（包括 SYN 和 FIN）分配序列号，确保数据正确排序且无重复。
            - 发送端还要根据接收方的 ACK 来管理 **哪些数据已成功接收**，并重传未确认的部分。
        3. **重传机制**：
            - 发送方需要在 **丢包时进行重传**，而重传并不是简单的再发一次数据。它涉及到 **超时计时器**（RTO）、**重传计数**、**指数回退**等机制，这一切都需要发送方动态地调整超时时间。
        4. **三次握手和流控制中的交互**：
            - 在发送方的实现中，**三次握手**的处理、**流控制**、**数据划分、发送、确认**、**连接管理**等机制都要协调配合。
            - 这些机制使得发送方的代码变得相对复杂，涉及到很多的状态管理。
        
        好问题！我们现在就用**一个完整的例子**来讲清楚：
        
        - 发送端是怎么发 TCP 报文的？
        - 接收端是怎么回应 ACK 的？
        - 如果丢包了，重传机制又是如何介入工作的？
        
        ---
        
        ## ✅ 一、TCP 报文正常发送 & ACK 响应流程
        
        假设：发送端要发字符串 `"HELLO"`，5 个字节。
        
        我们假设窗口允许一次最多发 3 个字节（window_size = 3）
        
        ---
        
        ### 步骤一：发送端调用 `fill_window()`
        
        它做了什么？
        
        1. 看到窗口有空间（还可以发 3 个字节）；
        2. 从 ByteStream 中读出 `"HEL"`（3 字节）；
        3. 创建一个 TCP 段，假设 ISN 是 `1000`，则：
            - `seqno = wrap(1001)`（SYN 占了 1000）
            - payload = `"HEL"`
            - 长度 = 3
        4. 把这个 segment 放入 `segments_out`，上层会把它发出；
        5. 同时放入 `outstanding_segments`，以便等待 ACK；
        6. 启动重传计时器。
        
        ---
        
        ### 步骤二：接收端调用 `segment_received()`
        
        1. 接收到段 `"HEL"`，unwrap 得到绝对 seqno = 1（从 ISN + 1）；
        2. 把内容放入 StreamReassembler；
        3. 更新 ByteStream，变成 `"HEL"`；
        4. 返回 ackno = `wrap(1001 + 3) = wrap(1004)`；
        5. 返回 window_size，比如还能收 2 字节；
        
        ---
        
        ### 步骤三：发送端收到 ACK，调用 `ack_received()`
        
        1. 看 `ackno = wrap(1004)`，unwrap 得到 ack 的绝对位置是 4；
        2. 知道 `"HEL"` 那一段被确认了（seqno 范围 1~4）；
        3. 从 `outstanding_segments` 中移除那段；
        4. 更新 `bytes_in_flight -= 3`；
        5. 重置 RTO、清零重传次数、重启定时器；
        6. 看到窗口又有空 → 调用 `fill_window()` 再发后面 `"LO"`。
        
        ---
        
        ## ✅ 二、发生丢包后，重传机制如何工作？
        
        现在我们改个场景：
        
        - 发送 `"HEL"` 成功；
        - `"LO"` 的 segment 被发出，但 **ACK 丢了**；
        - 怎么办？
        
        ---
        
        ### 步骤一：tick 开始计时
        
        每次你调用 `tick(ms)`，发送方会：
        
        1. 累加 `ms`；
        2. 检查 oldest segment 发出后经过的时间是否 > 当前 RTO；
        3. 如果超时了：
            - 重发该段（把它重新推入 `segments_out()`）；
            - 重传计数 +1；
            - RTO *= 2；
            - 重启计时器；
        
        ---
        
        ### 步骤二：最终 ACK 到达
        
        假如后面 ACK `"LO"` 的报文终于到了：
        
        - ack_received() 会检查 ackno；
        - 找到 `"LO"` 对应的段被确认了；
        - 清空 `outstanding_segments`；
        - RTO 恢复初始值；
        - 重传计数清零。
        
        ---
        
        ## ✅ 总结这个流程的要点
        
        | 步骤 | 谁做的 | 调用的接口 | 说明 |
        | --- | --- | --- | --- |
        | 拆分并发送数据 | 发送方 | `fill_window()` | 参考窗口，生成 segment 推入发送队列 |
        | 接收并确认 | 接收方 | `segment_received()` + `ackno()` | 解码序列号、拼接数据、返回 ACK |
        | 处理 ACK | 发送方 | `ack_received()` | 判断哪些段被确认，释放资源、发送新数据 |
        | 检测超时 | 发送方 | `tick()` | 检查 oldest segment 是否超时 |
        | 重传段 | 发送方 | `segments_out().push()` | 把未 ACK 的段重新发出 |
        
        ---
        
        如果你想下一步，我们可以来手写一个 TCP 报文发送 + ack + 重传的小状态图，或者你说个你想模拟的场景，我来带你过一遍它的状态变迁！要不要来？
        
- 接口实现细节
    
    
    - 【cs144】✅ 总结：Lab3 本质是在实现什么？
        
        > 一个最小化、面向可靠性的 TCP 发送器：
        > 
        - **切片 → 发送 → 等待 ACK → 超时重发**
        - **动态滑动窗口**控制
        - ACK 驱动状态推进 + 重传计时器
    - 关键场景实现示例
        1. **建立连接场景**：
            
            ```cpp
            // 客户端发起连接
            fill_window() {
                发送SYN;
                等待SYN+ACK;
            }
            
            // 收到服务器响应
            ack_received() {
                确认SYN;
                进入ESTABLISHED状态;
            }
            
            ```
            
        2. **数据传输场景**：
            
            ```cpp
            // 发送数据
            fill_window() {
                while (窗口有空间 && 有数据) {
                    发送数据段;
                    追踪未确认段;
                }
            }
            
            // 处理确认
            ack_received() {
                移除已确认段;
                继续发送新数据;
            }
            
            ```
            
        3. **处理丢包场景**：
            
            ```cpp
            // 超时检测
            tick() {
                if (超时 && 未收到ACK) {
                    重传最早的段;
                    增加重传间隔;
                }
            }
            
            ```
            
    
    ### 1. fill_window() ：字节流转window再发出去
    
    1. **工作原理**：
        - 负责将**ByteStream**中的**数据包装成TCP段**并发送
        - 需要**遵守**接收方窗口大小限制
        - 处理**SYN和FIN标志位**
    2. **具体步骤**：
        
        ```cpp
        // 第一步：处理SYN（仅首次连接时）
        if (next_seqno == 0) {
            发送SYN段;
            更新计数器;
            return;
        }
        
        // 第二步：填充数据
        while (窗口未满 && 有数据可发送) {
            创建新的TCP段;
            从ByteStream读取数据;
            设置序号;
            加入发送队列;
            更新计数器;
        }
        
        // 第三步：处理FIN（流结束时）
        if (流结束 && 窗口够放FIN) {
            发送FIN段;
            更新状态;
        }
        
        ```
        
    
    ### 2. ack_received() ：在发完报文、connect和被对方通知改窗口大小的时候，更新窗口并处理ack
    
    1. **调用时机**：
        - **收到接收方的确认报文ack时**
        - 连接建立过程中收到SYN+ACK时
        - 对方**窗口大小**发生变化时
    2. **工作原理**：
        - 处理接收方的确认信息
        - 更新发送窗口
        - 触发新数据发送
    3. **设计原理**：
        - 实现**累积确认**：只要收到更大的ACK就清理之前所有段
        - 处理**流量控制**：根据对方通告窗口调整发送策略
    4. 场景示例：
        1. 正常确认：
        发送序号1000-2000，收到ACK=2000
        清理所有序号≤2000的段
        2. 重复确认：
        多次收到ACK=1000
        可能暗示丢包，准备重传
        
        ![1000240461.jpg](attachment:653ce151-e86a-45f6-b5b8-223f343d08ba:1000240461.jpg)
        
        ![1000240460.jpg](attachment:3aa5f7d0-dea9-4378-8527-5d51dd99730d:1000240460.jpg)
        
    5. **具体步骤**：
        
        ```cpp
        // 第一步：验证ACK合法性
        if (ackno无效) return;
        
        // 第二步：更新发送窗口
        更新window_size;
        
        // 第三步：清理已确认段
        while (队列非空 && 最早的段被完全确认) {
            更新bytes_in_flight;
            从队列移除;
            更新重传计时器;
        }
        
        // 第四步：有新空间则继续发送
        fill_window();
        
        ```
        
    
    ### 3. tick() 实现思路
    
    1. **调用时机**：
        - 系统定时器周期性调用（比如每10ms）
        - TCPConnection定期检查超时情况
    2. **设计原理**：
        - 实现超时重传机制：保证可靠传输
        - 实现拥塞控制：通过指数退避避免网络拥塞
    3. **工作原理**：
        - 处理超时重传
        - 实现指数退避
    4. **具体步骤**：
        
        ```cpp
        // 第一步：更新计时器
        timer.tick(ms_since_last_tick);
        
        // 第二步：检查是否需要重传
        if (timer超时 && 有未确认段) {
            重传最早的未确认段;
        
            if (window_size > 0) {
                consecutive_retransmissions++;
                RTO *= 2;  // 指数退避
            }
        
            重启计时器;
        }
        
        ```
        
    
    ### 5. 优化建议
    
    1. **性能优化**：
        - 合并小数据包减少网络开销
        - 动态调整重传超时时间
        - 适当缓存热点数据
    2. **健壮性优化**：
        - 完善错误处理
        - 增加边界检查
        - 防止序号回绕
    3. **调试建议**：
        - 记录关键事件日志
        - 跟踪序号变化
        - 监控重传次数
## ✅ 三、深入解析 send_retx.cc 测试设计思路

### 1. 测试框架设计思路

send_retx.cc 的测试设计体现了TCP协议中最核心的**可靠传输**机制。测试用例通过**状态机 + 事件驱动**的方式，精确模拟了TCP在各种网络条件下的行为。

#### 1.1 核心测试思路

1. **状态驱动测试**
   ```cpp
   // 示例：状态转换测试
   test.execute(ExpectState{TCPSenderStateSummary::SYN_SENT});  // 检查初始状态
   test.execute(AckReceived{WrappingInt32{isn + 1}});          // 触发状态转换
   test.execute(ExpectState{TCPSenderStateSummary::SYN_ACKED}); // 验证新状态
   ```

2. **时间精确控制**
   ```cpp
   // 示例：精确的超时控制
   test.execute(Tick{retx_timeout - 1});  // 超时前一刻
   test.execute(ExpectNoSegment{});       // 确保没有提前重传
   test.execute(Tick{1});                 // 触发超时重传
   ```

### 2. 关键场景测试流程

#### 2.1 连接建立的健壮性测试

1. **正常流程**:
   ```
   发送SYN -> 等待ACK -> 收到ACK -> ESTABLISHED
   ```

2. **丢包处理**:
   ```
   发送SYN -> 超时 -> 重传SYN -> 收到ACK -> ESTABLISHED
   ```

3. **示例代码解析**:
   ```cpp
   // 1. 初始SYN发送
   test.execute(ExpectSegment{}
       .with_syn(true)
       .with_seqno(isn));
   
   // 2. 模拟超时重传
   test.execute(Tick{retx_timeout});
   test.execute(ExpectSegment{} // 期待相同的SYN重传
       .with_syn(true)
       .with_seqno(isn));
   ```

#### 2.2 数据传输可靠性测试

1. **流程设计**:
   ```
   写入数据 -> 分段发送 -> 模拟丢包 -> 触发重传 -> 验证重传行为
   ```

2. **关键测试点**:
   ```cpp 
   // 1. 数据写入和发送
   test.execute(WriteBytes{"abcd"});
   test.execute(ExpectSegment{}.with_payload_size(4));
   
   // 2. 模拟丢包和重传
   test.execute(Tick{retx_timeout});
   test.execute(ExpectSegment{}.with_payload_size(4)); // 验证重传
   ```

### 3. 重传机制的精确性测试

#### 3.1 **指数退避机制**