/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include <cstdlib>
#include "Buffer.hpp"


namespace toolkit {
StatisticImp(Buffer)
StatisticImp(BufferRaw)
StatisticImp(BufferLikeString)

BufferRaw::Ptr BufferRaw::create() {
    return Ptr(new BufferRaw);
}

void BufferRaw::setCapacity(size_t capacity)
{
    if (_data) {
        do {
            if (capacity > _capacity) {
                //请求的内存大于当前内存，那么重新分配
                break;
            }

            if (_capacity < 2 * 1024) {
                //2K以下，不重复开辟内存，直接复用
                return;
            }

            if (2 * capacity > _capacity) {
                //如果请求的内存大于当前内存的一半，那么也复用
                return;
            }
        } while (false);

        delete[] _data;
    }
    _data = new char[capacity];
    _capacity = capacity;
}

void BufferRaw::setSize(size_t size)
{
    if (size > _capacity) {
        throw std::invalid_argument("Buffer::setSize out of range");
    }
    _size = size;
}

void BufferRaw::assign(const char *data, size_t size /*= 0*/)
{
    if (size <= 0) {
        size = strlen(data);
    }
    setCapacity(size + 1);
    memcpy(_data, data, size);
    _data[size] = '\0';
    setSize(size);
}

BufferLikeString & BufferLikeString::operator=(const BufferLikeString &that)
{
    _str = that._str;
    _erase_head = that._erase_head;
    _erase_tail = that._erase_tail;
    return *this;
}

BufferLikeString::BufferLikeString(const BufferLikeString &that)
{
    _str = that._str;
    _erase_head = that._erase_head;
    _erase_tail = that._erase_tail;
}

BufferLikeString::BufferLikeString(BufferLikeString &&that)
{
    _str = std::move(that._str);
    _erase_head = that._erase_head;
    _erase_tail = that._erase_tail;
    that._erase_head = 0;
    that._erase_tail = 0;
}

BufferLikeString::BufferLikeString(const char *str)
{
    if (str)
        _str = str;
    _erase_head = 0;
    _erase_tail = 0;
}

BufferLikeString::BufferLikeString(std::string str)
{
    _str = std::move(str);
    _erase_head = 0;
    _erase_tail = 0;
}

BufferLikeString::BufferLikeString()
{
    _erase_head = 0;
    _erase_tail = 0;
}

BufferLikeString & BufferLikeString::operator=(std::string str)
{
    _str = std::move(str);
    _erase_head = 0;
    _erase_tail = 0;
    return *this;
}

BufferLikeString & BufferLikeString::operator=(const char *str)
{
    if (str)
        _str = str;
    _erase_head = 0;
    _erase_tail = 0;
    return *this;
}

BufferLikeString & BufferLikeString::operator=(BufferLikeString &&that)
{
    _str = std::move(that._str);
    _erase_head = that._erase_head;
    _erase_tail = that._erase_tail;
    that._erase_head = 0;
    that._erase_tail = 0;
    return *this;
}

char * BufferLikeString::data() const
{
    return (char *)_str.data() + _erase_head;
}

size_t BufferLikeString::size() const
{
    return _str.size() - _erase_tail - _erase_head;
}

BufferLikeString & BufferLikeString::erase(size_t pos /*= 0*/, size_t n /*= std::string::npos*/)
{
    if (pos == 0) {
        //移除前面的数据
        if (n != std::string::npos) {
            //移除部分
            if (n > size()) {
                //移除太多数据了
                throw std::out_of_range("BufferLikeString::erase out_of_range in head");
            }
            //设置起始偏移量
            _erase_head += n;
            data()[size()] = '\0';
            return *this;
        }
        //移除全部数据
        _erase_head = 0;
        _erase_tail = _str.size();
        data()[0] = '\0';
        return *this;
    }

    if (n == std::string::npos || pos + n >= size()) {
        //移除末尾所有数据
        if (pos >= size()) {
            //移除太多数据
            throw std::out_of_range("BufferLikeString::erase out_of_range in tail");
        }
        _erase_tail += size() - pos;
        data()[size()] = '\0';
        return *this;
    }

    //移除中间的
    if (pos + n > size()) {
        //超过长度限制
        throw std::out_of_range("BufferLikeString::erase out_of_range in middle");
    }
    _str.erase(_erase_head + pos, n);
    return *this;
}

BufferLikeString & BufferLikeString::append(const char *data, size_t len)
{
    if (len <= 0) {
        return *this;
    }
    if (_erase_head > _str.capacity() / 2) {
        moveData();
    }
    if (_erase_tail == 0) {
        _str.append(data, len);
        return *this;
    }
    _str.insert(_erase_head + size(), data, len);
    return *this;
}

BufferLikeString & BufferLikeString::append(const char *data)
{
    return append(data, strlen(data));
}

BufferLikeString & BufferLikeString::append(const std::string &str)
{
    return append(str.data(), str.size());
}

BufferLikeString & BufferLikeString::append(const BufferLikeString &str)
{
    return append(str.data(), str.size());
}

void BufferLikeString::push_back(char c)
{
    if (_erase_tail == 0) {
        _str.push_back(c);
        return;
    }
    else {
        data()[size()] = c;
        --_erase_tail;
        data()[size()] = '\0';
    }
}

BufferLikeString & BufferLikeString::insert(size_t pos, const char *s, size_t n)
{
    _str.insert(_erase_head + pos, s, n);
    return *this;
}

BufferLikeString & BufferLikeString::assign(const char *data, size_t len)
{
    if (len <= 0) {
        return *this;
    }
    // data is in range of str, then modify _erase_head and _erase_tail
    if (data >= _str.data() && data < _str.data() + _str.size()) {
        _erase_head = data - _str.data();
        if (data + len > _str.data() + _str.size()) {
            throw std::out_of_range("BufferLikeString::assign out_of_range");
        }
        _erase_tail = _str.data() + _str.size() - (data + len);
    }
    else {
        _str.assign(data, len);
        _erase_head = 0;
        _erase_tail = 0;
    }
    return *this;
}

BufferLikeString & BufferLikeString::assign(const char *data)
{
    return assign(data, strlen(data));
}

void BufferLikeString::clear()
{
    _erase_head = 0;
    _erase_tail = 0;
    _str.clear();
}

const char & BufferLikeString::operator[](size_t pos) const
{
    return (*const_cast<BufferLikeString *>(this))[pos];
}

char & BufferLikeString::operator[](size_t pos)
{
    if (pos >= size()) {
        throw std::out_of_range("BufferLikeString::operator[] out_of_range");
    }
    return data()[pos];
}

size_t BufferLikeString::capacity() const
{
    return _str.capacity();
}

void BufferLikeString::reserve(size_t size)
{
    _str.reserve(size);
}

void BufferLikeString::resize(size_t size, char c /*= '\0'*/)
{
    _str.resize(size, c);
    _erase_head = 0;
    _erase_tail = 0;
}

bool BufferLikeString::empty() const
{
    return size() <= 0;
}

std::string BufferLikeString::substr(size_t pos, size_t n /*= std::string::npos*/) const
{
    if (n == std::string::npos) {
        //获取末尾所有的
        if (pos >= size()) {
            throw std::out_of_range("BufferLikeString::substr out_of_range");
        }
        return _str.substr(_erase_head + pos, size() - pos);
    }

    //获取部分
    if (pos + n > size()) {
        throw std::out_of_range("BufferLikeString::substr out_of_range");
    }
    return _str.substr(_erase_head + pos, n);
}

void BufferLikeString::moveData()
{
    if (_erase_head) {
        _str.erase(0, _erase_head);
        _erase_head = 0;
    }
}

}//namespace toolkit
