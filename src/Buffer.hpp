/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef ZLTOOLKIT_BUFFER_H
#define ZLTOOLKIT_BUFFER_H

#include <cassert>
#include <memory>
#include <string>
#include <vector>
#include <stdexcept>
#include <type_traits>
#include <functional>
#include "Util/util.h"
// use for BufferRaw friend declare
#include "Util/ResourcePool.h"
namespace toolkit {

template <typename T> struct is_pointer : public std::false_type {};
template <typename T> struct is_pointer<std::shared_ptr<T> > : public std::true_type {};
template <typename T> struct is_pointer<std::shared_ptr<T const> > : public std::true_type {};
template <typename T> struct is_pointer<T*> : public std::true_type {};
template <typename T> struct is_pointer<const T*> : public std::true_type {};

//缓存基类
class Buffer : public toolkit::noncopyable {
public:
    using Ptr = std::shared_ptr<Buffer>;

    Buffer() = default;
    virtual ~Buffer() = default;

    //返回数据长度
    virtual char *data() const = 0;
    virtual size_t size() const = 0;

    virtual std::string toString() const {
        return std::string(data(), size());
    }

    virtual size_t getCapacity() const {
        return size();
    }

private:
    //对象个数统计
    ObjectStatistic<Buffer> _statistic;
};

/*
要求 C 必须有 data 和 size 方法
*/
template <typename C>
class BufferOffset : public  Buffer {
public:
    using Ptr = std::shared_ptr<BufferOffset>;

    BufferOffset(C data, size_t offset = 0, size_t len = 0) : _data(std::move(data)) {
        setup(offset, len);
    }

    ~BufferOffset() override = default;

    char *data() const override {
        return const_cast<char *>(getPointer<C>(_data)->data()) + _offset;
    }

    size_t size() const override {
        return _size;
    }

    std::string toString() const override {
        return std::string(data(), size());
    }

private:
    void setup(size_t offset = 0, size_t size = 0) {
        auto max_size = getPointer<C>(_data)->size();
        assert(offset + size <= max_size);
        if (!size) {
            size = max_size - offset;
        }
        _size = size;
        _offset = offset;
    }

    template<typename T>
    static typename std::enable_if<is_pointer<T>::value, const T &>::type
    getPointer(const T &data) {
        return data;
    }

    template<typename T>
    static typename std::enable_if<!is_pointer<T>::value, const T *>::type
    getPointer(const T &data) {
        return &data;
    }

private:
    C _data;
    size_t _size;
    size_t _offset;
};

using BufferString = BufferOffset<std::string>;

//指针式缓存对象，
class BufferRaw : public Buffer {
public:
    using Ptr = std::shared_ptr<BufferRaw>;

    static Ptr create();

    ~BufferRaw() override {
        if (_data) {
            delete[] _data;
        }
    }

    //在写入数据时请确保内存是否越界
    char *data() const override {
        return _data;
    }

    //有效数据大小
    size_t size() const override {
        return _size;
    }

    //分配内存大小
    void setCapacity(size_t capacity);

    //设置有效数据大小
    virtual void setSize(size_t size);

    //赋值数据
    void assign(const char *data, size_t size = 0);

    size_t getCapacity() const override {
        return _capacity;
    }

protected:
    friend class toolkit::ResourcePool_l<BufferRaw>;

    BufferRaw(size_t capacity = 0) {
        if (capacity) {
            setCapacity(capacity);
        }
    }

    BufferRaw(const char *data, size_t size = 0) {
        assign(data, size);
    }

private:
    size_t _size = 0;
    size_t _capacity = 0;
    char *_data = nullptr;
    //对象个数统计
    ObjectStatistic<BufferRaw> _statistic;
};

class BufferLikeString : public Buffer {
public:
    ~BufferLikeString() override = default;

    BufferLikeString();
    BufferLikeString(std::string str);
    BufferLikeString(const char *str);
    BufferLikeString(BufferLikeString &&that);
    BufferLikeString(const BufferLikeString &that);

    BufferLikeString &operator=(std::string str);
    BufferLikeString &operator=(const char *str);
    BufferLikeString &operator=(BufferLikeString &&that);
    BufferLikeString &operator=(const BufferLikeString &that);

    char *data() const override;
    size_t size() const override;

    BufferLikeString &erase(size_t pos = 0, size_t n = std::string::npos);
    BufferLikeString &append(const BufferLikeString &str);
    BufferLikeString &append(const std::string &str);
    BufferLikeString &append(const char *data);
    BufferLikeString &append(const char *data, size_t len);

    void push_back(char c);

    BufferLikeString &insert(size_t pos, const char *s, size_t n);
    BufferLikeString &assign(const char *data);
    BufferLikeString &assign(const char *data, size_t len);

    void clear();

    char &operator[](size_t pos);
    const char &operator[](size_t pos) const;

    size_t capacity() const;

    void reserve(size_t size);

    void resize(size_t size, char c = '\0');

    bool empty() const;

    std::string substr(size_t pos, size_t n = std::string::npos) const;

private:
    void moveData();

private:
    size_t _erase_head;
    size_t _erase_tail;
    std::string _str;
    //对象个数统计
    ObjectStatistic<BufferLikeString> _statistic;
};

}//namespace toolkit
#endif //ZLTOOLKIT_BUFFER_H
