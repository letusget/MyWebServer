#include "buffer.h"

//初始大小为默认初始化1024
Buffer::Buffer(int initBuffSize) : buffer_(initBuffSize), readPos_(0), writePos_(0) {}

// 可以读的数据的大小  写位置 - 读位置，中间的数据就是可以读的大小
size_t Buffer::ReadableBytes() const
{
    return writePos_ - readPos_;
}

// 可以写的数据大小，缓冲区的总大小 - 写位置
size_t Buffer::WritableBytes() const
{
    return buffer_.size() - writePos_;
}

// 前面可以用的空间，当前读取到哪个位置，就是前面可以用的空间大小
size_t Buffer::PrependableBytes() const
{
    return readPos_;
}

const char *Buffer::Peek() const
{
    return BeginPtr_() + readPos_;
}

void Buffer::Retrieve(size_t len)
{
    //读取的数据长度小于 可以放置的空间的大小
    assert(len <= ReadableBytes());
    //调整下次可以放入读的信息 的位置
    readPos_ += len;
}

// buff.RetrieveUntil(lineEnd + 2);
void Buffer::RetrieveUntil(const char *end)
{
    assert(Peek() <= end);
    Retrieve(end - Peek());
}

void Buffer::RetrieveAll()
{
    bzero(&buffer_[0], buffer_.size());
    readPos_ = 0;
    writePos_ = 0;
}

std::string Buffer::RetrieveAllToStr()
{
    std::string str(Peek(), ReadableBytes());
    RetrieveAll();
    return str;
}

const char *Buffer::BeginWriteConst() const
{
    return BeginPtr_() + writePos_;
}

char *Buffer::BeginWrite()
{
    return BeginPtr_() + writePos_;
}

void Buffer::HasWritten(size_t len)
{
    writePos_ += len;
}

void Buffer::Append(const std::string &str)
{
    Append(str.data(), str.length());
}

void Buffer::Append(const void *data, size_t len)
{
    assert(data);
    Append(static_cast<const char *>(data), len);
}

//  Append(buff, len - writable);   buff临时数组，len-writable是临时数组中的数据个数
void Buffer::Append(const char *str, size_t len)
{
    assert(str);
    EnsureWriteable(len);
    std::copy(str, str + len, BeginWrite());
    HasWritten(len);
}

void Buffer::Append(const Buffer &buff)
{
    Append(buff.Peek(), buff.ReadableBytes());
}

void Buffer::EnsureWriteable(size_t len)
{
    if (WritableBytes() < len)
    {
        MakeSpace_(len);
    }
    assert(WritableBytes() >= len);
}

ssize_t Buffer::ReadFd(int fd, int *saveErrno)
{

    char buff[65535]; // 临时的数组，保证能够把所有的数据都读出来

    //结构体数组
    struct iovec iov[2];
    const size_t writable = WritableBytes();

    /* 分散读， 保证数据全部读完 */
    iov[0].iov_base = BeginPtr_() + writePos_; //读取内存的地址+写入的长度，表示下次要写入的开始的位置
    iov[0].iov_len = writable;
    iov[1].iov_base = buff; //临时数组的位置
    iov[1].iov_len = sizeof(buff);

    const ssize_t len = readv(fd, iov, 2);
    if (len < 0)
    {
        *saveErrno = errno;
    }
    else if (static_cast<size_t>(len) <= writable)
    {
        writePos_ += len;
    }
    else
    {
        writePos_ = buffer_.size();
        //追加 放入数据
        Append(buff, len - writable);
    }
    return len;
}

ssize_t Buffer::WriteFd(int fd, int *saveErrno)
{
    size_t readSize = ReadableBytes();
    ssize_t len = write(fd, Peek(), readSize);
    if (len < 0)
    {
        *saveErrno = errno;
        return len;
    }
    readPos_ += len;
    return len;
}

char *Buffer::BeginPtr_()
{
    return &*buffer_.begin();
}

const char *Buffer::BeginPtr_() const
{
    return &*buffer_.begin();
}

void Buffer::MakeSpace_(size_t len)
{
    //可用空间 与 预留空间的长度
    if (WritableBytes() + PrependableBytes() < len)
    {
        //调整空间大小，进行扩容
        buffer_.resize(writePos_ + len + 1);
    }
    else
    {
        size_t readable = ReadableBytes();
        std::copy(BeginPtr_() + readPos_, BeginPtr_() + writePos_, BeginPtr_());
        readPos_ = 0;
        writePos_ = readPos_ + readable;
        assert(readable == ReadableBytes());
    }
}