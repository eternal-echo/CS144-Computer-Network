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
StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity), _capacity(capacity) {
    _buffer.resize(capacity);
}

long StreamReassembler::merge_block(block_node &elm1, const block_node &elm2) {
    block_node x, y;
    if (elm1.begin > elm2.begin) {
        x = elm2;
        y = elm1;
    } else {
        x = elm1;
        y = elm2;
    }
    if (x.begin + x.length < y.begin) {
        return -1;  // no intersection, couldn't merge
    } else if (x.begin + x.length >= y.begin + y.length) {
        elm1 = x;
        return y.length;
    } else {
        elm1.begin = x.begin;
        elm1.data = x.data + y.data.substr(x.begin + x.length - y.begin);
        elm1.length = elm1.data.length();
        return x.begin + x.length - y.begin;
    }
}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
/*!
 * \brief 将数据子串推入重组器进行重组
 * \param data 待重组的数据子串
 * \param index 子串在原始数据流中的起始索引
 * \param eof 是否为数据流的结束标志
 * \details 该函数接收可能乱序到达的数据子串，将其按序重组并写入输出流中
 */
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    // 检查是否超出容量限制
    if (index >= _head_index + _capacity) {  
        if (eof) {
            _eof_flag = true;
            if (empty()) {
                _output.end_input();
            }
        }  
        return;
    }

    // 处理子串的前缀部分，确保只保留未处理的数据
    block_node elm;
    if (index + data.length() <= _head_index) {  // 如果整个子串都已经处理过，直接跳转到EOF判断
        if (eof) {
            _eof_flag = true;
            if (empty()) {
                _output.end_input();
            }
        }
        return;
    } else if (index < _head_index) {  // 如果子串部分重叠，只取未处理部分
        size_t offset = _head_index - index;
        elm.data.assign(data.begin() + offset, data.end());
        elm.begin = index + offset;
        elm.length = elm.data.length();
    } else {  // 完整保存新的子串
        elm.begin = index;
        elm.length = data.length();
        elm.data = data;
    }
    _unassembled_byte += elm.length;

    // 合并重叠的子串
    do {
        // 向后合并：与后续重叠的子串进行合并
        long merged_bytes = 0;
        auto iter = _blocks.lower_bound(elm);
        while (iter != _blocks.end() && (merged_bytes = merge_block(elm, *iter)) >= 0) {
            _unassembled_byte -= merged_bytes;
            _blocks.erase(iter);
            iter = _blocks.lower_bound(elm);
        }
        // 向前合并：与前面重叠的子串进行合并
        if (iter == _blocks.begin()) {
            break;
        }
        iter--;
        while ((merged_bytes = merge_block(elm, *iter)) >= 0) {
            _unassembled_byte -= merged_bytes;
            _blocks.erase(iter);
            iter = _blocks.lower_bound(elm);
            if (iter == _blocks.begin()) {
                break;
            }
            iter--;
        }
    } while (false);
    _blocks.insert(elm);

    // 将连续的数据写入ByteStream
    if (!_blocks.empty() && _blocks.begin()->begin == _head_index) {
        const block_node head_block = *_blocks.begin();
        // 更新头索引和未组装字节数
        size_t write_bytes = _output.write(head_block.data);
        _head_index += write_bytes;
        _unassembled_byte -= write_bytes;
        _blocks.erase(_blocks.begin());
    }
    // 处理EOF标志，在所有数据处理完成后结束输入
    if (eof) {
        _eof_flag = true;
    }
    if (_eof_flag && empty()) {
        _output.end_input();
    }
}

size_t StreamReassembler::unassembled_bytes() const { return _unassembled_byte; }

bool StreamReassembler::empty() const { return _unassembled_byte == 0; }
