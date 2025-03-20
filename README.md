# CS144 : 计算机网络

这个仓库包含了斯坦福CS144课程(计算机网络)的所有学习材料，你可以访问[课程网站](https://cs144.github.io)获取更多指导。

## 资源

[视频](https://www.youtube.com/watch?v=K9hV3igminw&list=PLEAYkSg4uSQ2dr0XO_Nwa5OcdEcaaELSG&index=109) : 在线课程视频 

[课程PPT](./notes) : 补充PPT

[实验讲义](./lab_handouts) : 详细的实验说明

[考试](./exams) : 期中/期末考试及答案

## 实验部分

这门课最精彩的部分是实验。总共有8个实验，指导你一步步实现完整的互联网基础设施。以下是每个实验的简要概述：

- Lab 0 : 简单的互联网应用
- Lab 1 - 4 : TCP协议的实现
    - Lab 1 : 流重组器
    - Lab 2 : TCP接收端
    - Lab 3 : TCP发送端
    - Lab 4 : TCP连接
- Lab 5 : 网络接口
- Lab 6 : IP路由
- Lab 7 : 整体集成（使用你自己的互联网基础设施互相通信，很酷！）

#### Lab 1 : 流重组器 (2-3小时)

相关文件：stream_reassembler.cc, stream_reassembler.hh

我的实现使用了固定大小滑动窗口的思想。使用变量`unass_base`记录第一个未组装字节的索引，这也是滑动窗口的第一个字节。为简化实现，我使用位图表示滑动窗口中每个字节的当前状态。当一个子字符串被推入重组器时，首先在位图中标记该子字符串与窗口的交集部分为已组装。然后调用`check_contiguous()`函数检查是否可以移动窗口，即组装更多字节。代码中一个技巧是为保持滑动窗口大小固定，当从窗口中移出一个元素时，也要向窗口中推入一个占位符。

注意：传入子字符串的eof并不意味着所有子字符串都已组装完成，因为这些子字符串可能以任意顺序到达，这意味着字符串的最后部分可能最先到达并标记eof为true，但你需要等待所有子字符串到达才能最终结束输入。

#### Lab 2 : TCP接收端 (4小时)

相关文件：wrapping_integers.cc, wrapping_integers.hh, tcp_receiver.cc, tcp_receiver.hh

代码很短，但要正确实现很棘手。我的实现有点混乱：(，但最终能工作：）。

#### Lab 3 : TCP发送端 (6小时)

相关文件：tcp_sender.cc, tcp_sender.hh

写代码前多思考还不够，建议至少阅读讲义5遍，在编码前理解每个细节。我大约1小时写完代码，但花了接下来痛苦的5小时调试。我认为实验最棘手的部分是`window`的概念以及当窗口大小为零时会发生什么。为了正确实现，建议使用一个变量记录接收方通告的窗口大小（我使用_recx_windowsize）并使用另一个变量记录接收方已确认的窗口右边缘（我使用rwindow）（你不需要记录左边缘，因为它等于_next_seqno）。希望下图能给你一些直观认识。从图中可以看出，每次调用fill_window()函数时，你只需要发送_next_seqno和rwindow之间的数据。

![image](./images/lab3_1.png)

现在花点时间充分理解第3.2节中的以下段落。

```plaintext
⋆如果窗口大小为零应该怎么办？
如果接收方通告的窗口大小为零，fill_window方法应该像窗口大小为1那样行动。发送方可能会发送一个被接收方拒绝（且不确认）的单字节，但这也可以促使接收方发送新的确认段，在其中透露更多空间已在其窗口中打开。没有这个机制，发送方将永远不会知道它被允许重新开始发送。
```

这里的"窗口大小"指的是接收方的实际窗口大小，即图中的窗口大小。只有当接收方表示其窗口大小为零时，你才应该执行上述操作。因此，当你发现_next_seqno == rwindow时，你应该检查windowsize的值。

此外，我强烈建议你在fill_window()中将第一个SYN包作为特殊情况处理。这将简化你的实现并节省大量时间。

#### Lab 4 : TCP连接 (3小时)

相关文件：tcp_connection.cc, tcp_connection.hh

提示：

- 发送方的fill_window()只会将段推入其自己的segments_out()，这与TCPConnection的segments_out()不同！因此，每当你调用可能发送段的发送方函数时，你需要从发送方的segments_out()中弹出段，添加ackno和window_size，然后将其推入TCPConnection的segments_out()

- 决定在流结束后何时需要延迟有点棘手。仔细阅读讲义第5.1节中的以下文字。

    ```
    实际上这意味着你的TCPConnection有一个名为linger_after_streams_finish的成员变量，通过state()方法暴露给测试装置。该变量开始时为true。如果入站流在TCPConnection到达其出站流的EOF之前结束，该变量需要设置为false。
    在满足前提条件#1到#3的任何时候，如果linger_after_streams_finish为false，则连接"完成"（且active()应返回false）。否则你需要延迟：仅当自最后一个段接收以来已经过足够时间（10×cfg.rt_timeout）后，连接才算完成。
    ```

    简而言之，你需要在入站流结束后立即检查！那么你什么时候第一次知道这一点？显然是当你在TCPConnection中调用segment_received时，因为那是你可能从对等方获得FIN的时候。

#### Lab 5 : 网络接口 (2小时)

简单直接，第一次成功编译后就通过了测试。

#### Lab 6 : IP路由 (1小时)

不要过分考虑效率，你可能想用Trie树来做最长前缀匹配，但暴力算法足以通过测试。记得处理测试设置中的特殊情况：0.0.0.0/0。

## Sponge快速入门

关于构建前提条件，请参见[CS144虚拟机设置说明](https://web.stanford.edu/class/cs144/vm_howto)。

```bash
docker pull vidocqh/cs144:latest

#使用后删除容器，并映射当前目录到容器的 /cs144 目录，进入cs144目录
docker run -it --rm --privileged -v $(pwd):/cs144 --name cs144_container --workdir /cs144 vidocqh/cs144 /bin/bash
# td是后台运行，privileged是为了使用tun/tap设备
docker run -td --privileged --name cs144_container vidocqh/cs144

sudo apt update && sudo apt install git cmake gdb build-essential clang clang-tidy clang-format gcc-doc pkg-config glibc-doc tcpdump tshark
```

要创建构建目录：

    $ mkdir -p <path/to/sponge>/build
    $ cd <path/to/sponge>/build
    $ cmake ..

注意：以下所有命令都应该在`build`目录中运行。

构建：

        $ make

你可以使用`-j`开关进行并行构建，例如：

        $ make -j$(nproc)

测试（在构建之后；确保已安装[构建前提条件](https://web.stanford.edu/class/cs144/vm_howto)！）：

        $ make check_lab0

或

    $ make check_lab1

等。

首次运行`make check`时，可能会运行`sudo`来配置两个[TUN](https://www.kernel.org/doc/Documentation/networking/tuntap.txt)设备用于测试。

### 构建选项

运行cmake时可以指定不同的编译器：

        $ CC=clang CXX=clang++ cmake ..

你也可以指定`CLANG_TIDY=`或`CLANG_FORMAT=`（见下面"其他有用的目标"）。

Sponge的构建系统支持几种不同的构建目标。默认情况下，cmake选择`Release`目标，启用常规优化。`Debug`目标启用调试并降低优化级别。选择`Debug`目标：

        $ cmake .. -DCMAKE_BUILD_TYPE=Debug

支持以下目标：

- `Release` - 优化
- `Debug` - 调试符号和`-Og`
- `RelASan` - 带[ASan](https://en.wikipedia.org/wiki/AddressSanitizer)和[UBSan](https://developers.redhat.com/blog/2014/10/16/gcc-undefined-behavior-sanitizer-ubsan/)的发布版本
- `RelTSan` - 带[ThreadSan](https://developer.mozilla.org/en-US/docs/Mozilla/Projects/Thread_Sanitizer)的发布版本
- `DebugASan` - 带ASan和UBSan的调试版本
- `DebugTSan` - 带ThreadSan的调试版本

当然，你可以组合以上所有选项，例如：

        $ CLANG_TIDY=clang-tidy-6.0 CXX=clang++-6.0 .. -DCMAKE_BUILD_TYPE=Debug

注意：如果要更改`CC`、`CXX`、`CLANG_TIDY`或`CLANG_FORMAT`，需要删除`build/CMakeCache.txt`并重新运行cmake。（对于`CMAKE_BUILD_TYPE`不需要这样做。）

### 其他有用的目标

生成文档（需要`doxygen`；输出将在`build/doc/`中）：

        $ make doc

代码静态检查（需要`clang-tidy`）：

        $ make -j$(nproc) tidy

运行cppcheck（需要`cppcheck`）：

        $ make cppcheck

格式化（需要`clang-format`）：

        $ make format

查看所有可用目标：

        $ make help
