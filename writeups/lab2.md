Lab 2 Writeup
=============

My name: [eternal-echo]

My SUNet ID: [your sunetid here]

This lab took me about [5.5] hours to do. I [did not] attend the lab session.

I worked with or talked about this assignment with: [please list other sunetids]

Program Structure and Design of the TCPReceiver and wrap/unwrap routines:
[
在这个实验中，我实现了 TCPReceiver 以及序列号的包装（wrap）和解包装（unwrap）功能：

1. **wrap/unwrap 函数设计**：
   - `wrap` 函数将绝对序列号转换为 32 位循环序列号，通过取模运算实现循环特性
   - `unwrap` 函数将 32 位循环序列号转换回绝对序列号，需要处理序列号回绕的情况，使用检查点（checkpoint）作为参考，选择最接近检查点的绝对序列号

2. **TCPReceiver 设计**：
   - 使用 StreamReassembler 进行数据重组
   - 通过可选变量 _isn 跟踪初始序列号，只在收到 SYN 包时设置
   - 在 segment_received 方法中，计算数据在流中的索引位置，并将数据交给重组器
   - 实现 ackno 方法返回下一个期望接收的序列号，考虑 SYN 和 FIN 的影响
   - 实现 window_size 方法返回接收窗口大小

3. **状态管理**：
   - LISTEN：初始状态，尚未收到 SYN
   - SYN_RECV：已收到 SYN，可以接收数据
   - FIN_RECV：已收到 FIN，流结束
]

Implementation Challenges:
[
1. **序列号回绕处理**：最具挑战性的部分是理解和正确实现 unwrap 函数，特别是处理序列号回绕情况。我需要理解模运算的性质，并找出最接近检查点的绝对序列号。

2. **流索引计算**：将 TCP 序列号空间映射到流索引空间需要特别注意，因为 SYN 和 FIN 标志会占用序列号但不对应实际数据字节。特别是在处理带有 SYN 标志的段时，需要正确设置起始位置。而处理 FIN 标志时，需要注意 FIN 不会占用序列号，但resembler需要end_input。
]

Remaining Bugs:
[
经过全面测试，我的实现已经通过了所有的测试用例。解决了主要的问题：

1. 初期的段索引计算错误 - 修复了不正确考虑 SYN 和 FIN 标志影响的错误
2. StreamReassembler 的连续块处理问题 - 修改为循环处理所有连续块
3. FIN 标志处理逻辑 - 确保在所有数据处理完成后才标记流结束

目前未发现其他潜在问题。
]

- Optional: I had unexpected difficulty with: [describe]

- Optional: I think you could make this lab better by: [一边结合代码的测试用例一边看gpt的原理讲解和handout，这样效率更高，多看测试用例就懂了]

- Optional: I was surprised by: [describe]

- Optional: I'm not sure about: [describe]
