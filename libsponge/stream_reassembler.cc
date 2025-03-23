#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.
// 流重组器的初始实现

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.
// 对于实验1，请替换为能通过 `make check_lab1` 自动检查的实际实现

// You will need to add private members to the class declaration in `stream_reassembler.hh`
// 你需要在 `stream_reassembler.hh` 中向类声明添加私有成员

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

// 构造函数：初始化流重组器
// capacity: 字节容量上限
StreamReassembler::StreamReassembler(const size_t capacity) : 
    unass_base(0),      // 未组装的基准索引（下一个期望接收的字节索引）
    unass_size(0),      // 已缓存但未组装的字节数量
    _eof(0),            // 是否已接收到EOF标志
    buffer(capacity, '\0'),  // 用于存储乱序到达的数据的缓冲区
    bitmap(capacity, false), // 记录缓冲区中各位置是否有有效数据的位图
    _output(capacity),       // 输出字节流
    _capacity(capacity) {}   // 容量上限

//! \details 这个函数在将子串推入输出流后调用
//! 它的目的是检查是否存在之前记录的连续子串可以被推入流中
void StreamReassembler::check_contiguous() {
    string tmp = "";
    while (bitmap.front()) {
        // 当缓冲区前端有连续数据时
        //cout<<"check one more contiguous substring"<<endl;
        tmp += buffer.front();     // 将数据添加到临时字符串
        buffer.pop_front();        // 移除缓冲区前端数据
        bitmap.pop_front();        // 移除对应的位图标记
        buffer.push_back('\0');    // 在缓冲区末尾添加空字符
        bitmap.push_back(false);   // 在位图末尾添加false标记
    }
    if (tmp.length() > 0) {
        cout << "push one contiguous substring with length " << tmp.length() << endl;
        _output.write(tmp);          // 将连续子串写入输出流
        unass_base += tmp.length();  // 更新基准索引
        unass_size -= tmp.length();  // 减少未组装字节计数
    } 
}

//! \details 此函数接受一个字节子串（即一个段），可能是乱序的，
//! 从逻辑流中，并组装任何新的连续子串，按顺序将它们写入输出流。
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    if (eof) {
        _eof = true;  // 记录EOF标志
    }
    size_t len = data.length();
    
    // 情况1：子串起始位置在当前基准位置之后或重合（处理新数据）
    if (index >= unass_base) {
        int offset = index - unass_base;  // 计算偏移量
        // 计算实际可以存储的长度：取数据长度和剩余容量的较小值
        size_t real_len = min(len, _capacity - _output.buffer_size() - offset);
        
        // 如果实际存储长度小于数据长度，表示部分数据被丢弃，重置EOF标志
        if (real_len < len) {
            _eof = false;
        }
        
        // 将数据存储到缓冲区中相应位置
        for (size_t i = 0; i < real_len; i++) {
            if (bitmap[i + offset]) continue;  // 跳过已存在的数据（避免重复）
            buffer[i + offset] = data[i];      // 存储数据
            bitmap[i + offset] = true;         // 更新位图
            unass_size++;                      // 增加未组装字节计数
        }
    } 
    // 情况2：子串起始位置在当前基准位置之前，但有部分数据可能是新的
    else if (index + len > unass_base) {
        int offset = unass_base - index;  // 计算有效数据的起始偏移
        // 计算实际可以存储的长度
        size_t real_len = min(len - offset, _capacity - _output.buffer_size());
        
        // 如果实际存储长度小于有效数据长度，重置EOF标志
        if (real_len < len - offset) {
            _eof = false;
        }
        
        // 将有效数据存储到缓冲区
        for (size_t i = 0; i < real_len; i++) {
            if (bitmap[i]) continue;      // 跳过已存在的数据
            buffer[i] = data[i + offset]; // 存储数据
            bitmap[i] = true;             // 更新位图
            unass_size++;                 // 增加未组装字节计数
        }
    }
    
    // 检查并处理连续的子串
    check_contiguous();
    
    // 如果收到EOF且所有数据都已组装完成，则结束输入
    if (_eof && unass_size == 0) {
        _output.end_input();
    }
}

// 返回已接收但尚未组装的字节数
size_t StreamReassembler::unassembled_bytes() const {
    return unass_size;
}

// 检查是否所有数据都已组装完成
bool StreamReassembler::empty() const {
    return unass_size == 0;
}
