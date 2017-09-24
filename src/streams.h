// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_STREAMS_H
#define BITCOIN_STREAMS_H

#include "support/allocators/zeroafterfree.h"
#include "serialize.h"
#include "util.h"

#include <algorithm>
#include <assert.h>
#include <ios>
#include <limits>
#include <map>
#include <set>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <utility>
#include <vector>

template<typename Stream>
class OverrideStream
{
    Stream* stream;

    const int nType;
    const int nVersion;

public:
    OverrideStream(Stream* stream_, int nType_, int nVersion_) : stream(stream_), nType(nType_), nVersion(nVersion_) {}

    template<typename T>
    OverrideStream<Stream>& operator<<(const T& obj)
    {
        // Serialize to this stream
        ::Serialize(*this, obj);
        return (*this);
    }

    template<typename T>
    OverrideStream<Stream>& operator>>(T& obj)
    {
        // Unserialize from this stream
        ::Unserialize(*this, obj);
        return (*this);
    }

    void write(const char* pch, size_t nSize)
    {
        stream->write(pch, nSize);
    }

    void read(char* pch, size_t nSize)
    {
        stream->read(pch, nSize);
    }

    int GetVersion() const { return nVersion; }
    int GetType() const { return nType; }
};

template<typename S>
OverrideStream<S> WithOrVersion(S* s, int nVersionFlag)
{
    return OverrideStream<S>(s, s->GetType(), s->GetVersion() | nVersionFlag);
}

/** Double ended buffer combining vector and stream-like interfaces.
 *
 * >> and << read and write unformatted data using the above serialization templates.
 * Fills with data in linear time; some stringstream implementations take N^2 time.
 */
class CDataStream
{
protected:
    typedef CSerializeData vector_type;
    vector_type vch;
    unsigned int nReadPos;

    int nType;
    int nVersion;
public:

    typedef vector_type::allocator_type   allocator_type;
    typedef vector_type::size_type        size_type;
    typedef vector_type::difference_type  difference_type;
    typedef vector_type::reference        reference;
    typedef vector_type::const_reference  const_reference;
    typedef vector_type::value_type       value_type;
    typedef vector_type::iterator         iterator;
    typedef vector_type::const_iterator   const_iterator;
    typedef vector_type::reverse_iterator reverse_iterator;

    explicit CDataStream(int nTypeIn, int nVersionIn)
    {
        Init(nTypeIn, nVersionIn);
    }

    CDataStream(const_iterator pbegin, const_iterator pend, int nTypeIn, int nVersionIn) : vch(pbegin, pend)
    {
        Init(nTypeIn, nVersionIn);
    }

#if !defined(_MSC_VER) || _MSC_VER >= 1300
    CDataStream(const char* pbegin, const char* pend, int nTypeIn, int nVersionIn) : vch(pbegin, pend)
    {
        Init(nTypeIn, nVersionIn);
    }
#endif

    CDataStream(const vector_type& vchIn, int nTypeIn, int nVersionIn) : vch(vchIn.begin(), vchIn.end())
    {
        Init(nTypeIn, nVersionIn);
    }

    CDataStream(const std::vector<char>& vchIn, int nTypeIn, int nVersionIn) : vch(vchIn.begin(), vchIn.end())
    {
        Init(nTypeIn, nVersionIn);
    }

    CDataStream(const std::vector<unsigned char>& vchIn, int nTypeIn, int nVersionIn) : vch(vchIn.begin(), vchIn.end())
    {
        Init(nTypeIn, nVersionIn);
    }

    template <typename... Args>
    CDataStream(int nTypeIn, int nVersionIn, Args&&... args)
    {
        Init(nTypeIn, nVersionIn);
        ::SerializeMany(*this, std::forward<Args>(args)...);
    }

    void Init(int nTypeIn, int nVersionIn)
    {
        nReadPos = 0;
        nType = nTypeIn;
        nVersion = nVersionIn;
    }

    CDataStream& operator+=(const CDataStream& b)
    {
        vch.insert(vch.end(), b.begin(), b.end());
        return *this;
    }

    friend CDataStream operator+(const CDataStream& a, const CDataStream& b)
    {
        CDataStream ret = a;
        ret += b;
        return (ret);
    }

    std::string str() const
    {
        return (std::string(begin(), end()));
    }


    //
    // Vector subset
    //
    const_iterator begin() const                     { return vch.begin() + nReadPos; }
    iterator begin()                                 { return vch.begin() + nReadPos; }
    const_iterator end() const                       { return vch.end(); }
    iterator end()                                   { return vch.end(); }
    size_type size() const                           { return vch.size() - nReadPos; }
    bool empty() const                               { return vch.size() == nReadPos; }
    void resize(size_type n, value_type c=0)         { vch.resize(n + nReadPos, c); }
    void reserve(size_type n)                        { vch.reserve(n + nReadPos); }
    const_reference operator[](size_type pos) const  { return vch[pos + nReadPos]; }
    reference operator[](size_type pos)              { return vch[pos + nReadPos]; }
    void clear()                                     { vch.clear(); nReadPos = 0; }
    iterator insert(iterator it, const char& x=char()) { return vch.insert(it, x); }
    void insert(iterator it, size_type n, const char& x) { vch.insert(it, n, x); }

    void insert(iterator it, std::vector<char>::const_iterator first, std::vector<char>::const_iterator last)
    {
        assert(last - first >= 0);
        if (it == vch.begin() + nReadPos && (unsigned int)(last - first) <= nReadPos)
        {
            // special case for inserting at the front when there's room
            nReadPos -= (last - first);
            memcpy(&vch[nReadPos], &first[0], last - first);
        }
        else
            vch.insert(it, first, last);
    }

#if !defined(_MSC_VER) || _MSC_VER >= 1300
    void insert(iterator it, const char* first, const char* last)
    {
        assert(last - first >= 0);
        if (it == vch.begin() + nReadPos && (unsigned int)(last - first) <= nReadPos)
        {
            // special case for inserting at the front when there's room
            nReadPos -= (last - first);
            memcpy(&vch[nReadPos], &first[0], last - first);
        }
        else
            vch.insert(it, first, last);
    }
#endif

    iterator erase(iterator it)
    {
        if (it == vch.begin() + nReadPos)
        {
            // special case for erasing from the front
            if (++nReadPos >= vch.size())
            {
                // whenever we reach the end, we take the opportunity to clear the buffer
                nReadPos = 0;
                return vch.erase(vch.begin(), vch.end());
            }
            return vch.begin() + nReadPos;
        }
        else
            return vch.erase(it);
    }

    iterator erase(iterator first, iterator last)
    {
        if (first == vch.begin() + nReadPos)
        {
            // special case for erasing from the front
            if (last == vch.end())
            {
                nReadPos = 0;
                return vch.erase(vch.begin(), vch.end());
            }
            else
            {
                nReadPos = (last - vch.begin());
                return last;
            }
        }
        else
            return vch.erase(first, last);
    }

    inline void Compact()
    {
        vch.erase(vch.begin(), vch.begin() + nReadPos);
        nReadPos = 0;
    }

    bool Rewind(size_type n)
    {
        // Rewind by n characters if the buffer hasn't been compacted yet
        if (n > nReadPos)
            return false;
        nReadPos -= n;
        return true;
    }


    //
    // Stream subset
    //
    bool eof() const             { return size() == 0; }
    CDataStream* rdbuf()         { return this; }
    int in_avail()               { return size(); }

    void SetType(int n)          { nType = n; }
    int GetType() const          { return nType; }
    void SetVersion(int n)       { nVersion = n; }
    int GetVersion() const       { return nVersion; }

    void read(char* pch, size_t nSize)
    {
        // Read from the beginning of the buffer
        unsigned int nReadPosNext = nReadPos + nSize;
        if (nReadPosNext >= vch.size())
        {
            if (nReadPosNext > vch.size())
            {
                throw std::ios_base::failure("CDataStream::read(): end of data");
            }
            memcpy(pch, &vch[nReadPos], nSize);
            nReadPos = 0;
            vch.clear();
            return;
        }
        memcpy(pch, &vch[nReadPos], nSize);
        nReadPos = nReadPosNext;
    }

    void ignore(int nSize)
    {
        // Ignore from the beginning of the buffer
        assert(nSize >= 0);
        unsigned int nReadPosNext = nReadPos + nSize;
        if (nReadPosNext >= vch.size())
        {
            if (nReadPosNext > vch.size())
                throw std::ios_base::failure("CDataStream::ignore(): end of data");
            nReadPos = 0;
            vch.clear();
            return;
        }
        nReadPos = nReadPosNext;
    }

    void write(const char* pch, size_t nSize)
    {
        // Write to the end of the buffer
        vch.insert(vch.end(), pch, pch + nSize);
    }

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        // Special case: stream << stream concatenates like stream += stream
        if (!vch.empty())
            s.write((char*)&vch[0], vch.size() * sizeof(vch[0]));
    }

    template<typename T>
    CDataStream& operator<<(const T& obj)
    {
        // Serialize to this stream
        ::Serialize(*this, obj);
        return (*this);
    }

    template<typename T>
    CDataStream& operator>>(T& obj)
    {
        // Unserialize from this stream
        ::Unserialize(*this, obj);
        return (*this);
    }

    void GetAndClear(CSerializeData &data) {
        data.insert(data.end(), begin(), end());
        clear();
    }

    /**
     * XOR the contents of this stream with a certain key.
     *
     * @param[in] key    The key used to XOR the data in this stream.
     */
    void Xor(const std::vector<unsigned char>& key)
    {
        if (key.size() == 0) {
            return;
        }

        for (size_type i = 0, j = 0; i != size(); i++) {
            vch[i] ^= key[j++];

            // This potentially acts on very many bytes of data, so it's
            // important that we calculate `j`, i.e. the `key` index in this
            // way instead of doing a %, which would effectively be a division
            // for each byte Xor'd -- much slower than need be.
            if (j == key.size())
                j = 0;
        }
    }
};










/** Non-refcounted RAII wrapper for FILE*
 *
 * Will automatically close the file when it goes out of scope if not null.
 * If you're returning the file pointer, return file.release().
 * If you need to close the file early, use file.fclose() instead of fclose(file).
 */
class CAutoFile
{
private:
    // Disallow copies
    CAutoFile(const CAutoFile&);
    CAutoFile& operator=(const CAutoFile&);

    const int nType;
    const int nVersion;

    FILE* file;	

public:
    CAutoFile(FILE* filenew, int nTypeIn, int nVersionIn) : nType(nTypeIn), nVersion(nVersionIn)
    {
        file = filenew;
    }

    ~CAutoFile()
    {
        fclose();
    }

    void fclose()
    {
        if (file) {
            ::fclose(file);
            file = NULL;
        }
    }

    /** Get wrapped FILE* with transfer of ownership.
     * @note This will invalidate the CAutoFile object, and makes it the responsibility of the caller
     * of this function to clean up the returned FILE*.
     */
    FILE* release()             { FILE* ret = file; file = NULL; return ret; }

    /** Get wrapped FILE* without transfer of ownership.
     * @note Ownership of the FILE* will remain with this class. Use this only if the scope of the
     * CAutoFile outlives use of the passed pointer.
     */
    FILE* Get() const           { return file; }

    /** Return true if the wrapped FILE* is NULL, false otherwise.
     */
    bool IsNull() const         { return (file == NULL); }

    //
    // Stream subset
    //
    int GetType() const          { return nType; }
    int GetVersion() const       { return nVersion; }

    void read(char* pch, size_t nSize)
    {
        if (!file)
            throw std::ios_base::failure("CAutoFile::read: file handle is NULL");
        if (fread(pch, 1, nSize, file) != nSize)
            throw std::ios_base::failure(feof(file) ? "CAutoFile::read: end of file" : "CAutoFile::read: fread failed");
    }

    void ignore(size_t nSize)
    {
        if (!file)
            throw std::ios_base::failure("CAutoFile::ignore: file handle is NULL");
        unsigned char data[4096];
        while (nSize > 0) {
            size_t nNow = std::min<size_t>(nSize, sizeof(data));
            if (fread(data, 1, nNow, file) != nNow)
                throw std::ios_base::failure(feof(file) ? "CAutoFile::ignore: end of file" : "CAutoFile::read: fread failed");
            nSize -= nNow;
        }
    }

    void write(const char* pch, size_t nSize)
    {
        if (!file)
            throw std::ios_base::failure("CAutoFile::write: file handle is NULL");
        if (fwrite(pch, 1, nSize, file) != nSize)
            throw std::ios_base::failure("CAutoFile::write: write failed");
    }

    template<typename T>
    CAutoFile& operator<<(const T& obj)
    {
        // Serialize to this stream
        if (!file)
            throw std::ios_base::failure("CAutoFile::operator<<: file handle is NULL");
        ::Serialize(*this, obj);
        return (*this);
    }

    template<typename T>
    CAutoFile& operator>>(T& obj)
    {
        // Unserialize from this stream
        if (!file)
            throw std::ios_base::failure("CAutoFile::operator>>: file handle is NULL");
        ::Unserialize(*this, obj);
        return (*this);
    }
};


/** Non-refcounted RAII wrapper around a FILE* that implements a ring buffer to
 *  deserialize from. It guarantees the ability to rewind a given number of bytes.
 *
 *  Will automatically close the file when it goes out of scope if not null.
 *  If you need to close the file early, use file.fclose() instead of fclose(file).
 */
class CBufferedFile
{
private:
    // Disallow copies
    CBufferedFile(const CBufferedFile&);
    CBufferedFile& operator=(const CBufferedFile&);

    const int nType;
    const int nVersion;

    FILE *src;            // source file
    uint64_t nSrcPos;     // how many bytes have been read from source
    uint64_t nReadPos;    // how many bytes caller has read from this
    uint64_t nReadLimit;  // up to which position we're allowed to read
    uint64_t nRewind;     // how many bytes we guarantee to rewind
    std::vector<char> vchBuf; // the buffer
    enum { RESIZE_EXTRA = 200000 };  // BU how much additional to allocate if forced to resize
protected:
    // read data from the source to fill the buffer
    bool Fill() {
        unsigned int pos = nSrcPos % vchBuf.size();
        unsigned int readNow = vchBuf.size() - pos;  // how much to go until the end
        unsigned int nAvail = vchBuf.size() - (nSrcPos - nReadPos) - nRewind;  // how much do we need to preserve
        if (nAvail < readNow)
            readNow = nAvail;
        if (readNow == 0)
            return false;
        size_t read = fread((void*)&vchBuf[pos], 1, readNow, src);
        if (read == 0) {
            throw std::ios_base::failure(feof(src) ? "CBufferedFile::Fill: end of file" : "CBufferedFile::Fill: fread failed");
        } else {
            nSrcPos += read;
            return true;
        }
    }

public:
    CBufferedFile(FILE *fileIn, uint64_t nBufSize, uint64_t nRewindIn, int nTypeIn, int nVersionIn) :
        nType(nTypeIn), nVersion(nVersionIn), nSrcPos(0), nReadPos(0), nReadLimit((uint64_t)(-1)), nRewind(nRewindIn), vchBuf(nBufSize, 0)
    {
        src = fileIn;
    }

    ~CBufferedFile()
    {
        fclose();
    }

    int GetVersion() const { return nVersion; }
    int GetType() const { return nType; }

    void fclose()
    {
        if (src) {
            ::fclose(src);
            src = NULL;
        }
    }

    // check whether we're at the end of the source file
    bool eof() const {
        return nReadPos == nSrcPos && feof(src);
    }

    // read a number of bytes
    void read(char *pch, size_t nSize) {
        if (nSize + nReadPos > nReadLimit)
            throw std::ios_base::failure("Read attempted past buffer limit");
        if (nSize + nRewind > vchBuf.size())  // What's already read + what I want to read + how far I want to rewind
          {
            LogPrint("reindex","Large read, growing buffer\n", nSize);
            GrowTo(nSize + nRewind + RESIZE_EXTRA);
            if (nSize + nRewind > vchBuf.size())  // make sure it worked
              throw std::ios_base::failure("Read larger than buffer size");
          }
        while (nSize > 0) {
            if (nReadPos == nSrcPos)
                Fill();
            unsigned int pos = nReadPos % vchBuf.size();
            size_t nNow = nSize;
            if (nNow + pos > vchBuf.size())
                nNow = vchBuf.size() - pos;
            if (nNow + nReadPos > nSrcPos)
                nNow = nSrcPos - nReadPos;
            memcpy(pch, &vchBuf[pos], nNow);
            nReadPos += nNow;
            pch += nNow;
            nSize -= nNow;
        }
    }

    // return the current reading position
    uint64_t GetPos() {
        return nReadPos;
    }

    // rewind to a given reading position
    bool SetPos(uint64_t nPos) {
        nReadPos = nPos;
        if (nReadPos + nRewind < nSrcPos) {
            LogPrint("reindex","Short SetPos: desired %lld actual %lld srcpos %lld buffer size %lld, rewind %lld\n", nPos, nReadPos, nSrcPos, vchBuf.size(), nRewind);
            nReadPos = nSrcPos - nRewind;
            return false;
        } else if (nReadPos > nSrcPos) {
            LogPrint("reindex","Long SetPos: desired %lld actual %lld srcpos %lld buffer size %lld, rewind %lld\n", nPos, nReadPos, nSrcPos, vchBuf.size(), nRewind);
            nReadPos = nSrcPos;
            return false;
        } else {
            return true;
        }
    }

#if 0  // BU: commented because this function is unused, and not correct -- after seeking, you can't SetPos and therefore correctly rewind...
    bool Seek(uint64_t nPos) {
        long nLongPos = nPos;
        if (nPos != (uint64_t)nLongPos)
            return false;
        if (fseek(src, nLongPos, SEEK_SET))
            return false;
        nLongPos = ftell(src);
        nSrcPos = nLongPos;
        nReadPos = nLongPos;
        return true;
    }
#endif

    // prevent reading beyond a certain position
    // no argument removes the limit
    bool SetLimit(uint64_t nPos = (uint64_t)(-1)) {
        if (nPos < nReadPos)
            return false;
        nReadLimit = nPos;
        return true;
    }

    template<typename T>
    CBufferedFile& operator>>(T& obj) {
        // Unserialize from this stream
        ::Unserialize(*this, obj);
        return (*this);
    }

    // search for a given byte in the stream, and remain positioned on it
    void FindByte(char ch) {
        while (true) {
            if (nReadPos == nSrcPos)
                Fill();
            if (vchBuf[nReadPos % vchBuf.size()] == ch)
                break;
            nReadPos++;
        }
    }

    // if the current buffer doesn't have amt more data, then extend it by that much
    void GrowTo(uint64_t amt)
    {
      if (vchBuf.size() < amt)  // We want as much data as we are currently saving, plus the new data
        {
          amt = std::max(amt, ((uint64_t)vchBuf.size())*2);  // Resize is inefficient, so at a minimum double the buffer to make # resizes log(n)
          vchBuf.resize(amt,0);
          LogPrint("reindex","File buffer resize to %s\n", vchBuf.size());

          // Now at this new buffer size the boundaries will be different so I have to reload the rewinded data
          uint64_t readPos = 0;  // Position the data to be read at the start of the old maximum rewind (or the file beginning)
          if (nRewind < nReadPos)
            readPos = nReadPos - nRewind;

          // Now expand the rewind
          nRewind = amt/2;

          if (fseek(src, readPos, SEEK_SET))
            {
            throw std::ios_base::failure("CBufferedFile::GrowTo: fseek error");
            }
          unsigned int pos = readPos % vchBuf.size();
          unsigned int readNow = std::min((uint64_t)(vchBuf.size() - pos), nRewind);  // the amount to read is the minimum of what's left over or the max we can read
          size_t read = fread((void*)&vchBuf[pos], 1, readNow, src);
          assert (read != 0);  // We MUST be able to read something because we rewound by nRewind so we've already read this once.
          nSrcPos = readPos + read;
          if ((nReadPos > nSrcPos)&&(read==readNow))  // I filled to the buffer end, but that wasn't enough
            {
              readNow = std::min(pos,(unsigned int) (nRewind-read));  // The limit of this read is the prior start position in the buffer, or the maximum ahead the read is allowed to get
              read = fread((void*)&vchBuf[0], 1, readNow, src);
              if (read == 0)
                {
                throw std::ios_base::failure(feof(src) ? "CBufferedFile::GrowTo: end of file" : "CBufferedFile::GrowTo: fread failed");
                }
              nSrcPos += read;
            }
          assert(nReadPos <= nSrcPos);  // By the end of the above logic, we must have filled the buffer up to the current read position.
        }
    }
};

#endif // BITCOIN_STREAMS_H
