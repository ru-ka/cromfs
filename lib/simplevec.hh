#ifndef bqtSimpleVecHH
#define bqtSimpleVecHH

#include <algorithm>
#include <string>
#include <deque>

//#define SIMPLEVEC_RANGECHECKING

class SimpleVecException: public std::bad_alloc
{
public:
    SimpleVecException(const std::string& s): msg(s) { }
    virtual const char* what() const throw() { return msg.c_str(); }
    virtual ~SimpleVecException() throw() { }
private:
    std::string msg;
};

template<typename T,size_t maxsize>
class SimpleVec
{
public:
    typedef const T* const_iterator;
    inline const_iterator begin() const { return data; }
    inline const_iterator end() const { return data+ptr; }
    inline const T& operator[] (size_t n) const { return data[n]; }

    typedef T* iterator;
    inline iterator begin() { return data; }
    inline iterator end() { return data+ptr; }
    inline T& operator[] (size_t n)
    {
#ifdef SIMPLEVEC_RANGECHECKING
        if(n >= maxsize) { throw SimpleVecException("operator[]"); }
#endif
        return data[n];
    }
public:
    SimpleVec() : data(datacontainer), ptr(0)
    {
        //std::fill(datacontainer,datacontainer+maxsize, T());
    }

    inline void push_back(T v) throw(SimpleVecException)
    {
#ifdef SIMPLEVEC_RANGECHECKING
        if(ptr >= maxsize) { throw SimpleVecException("push_back"); }
#endif
        data[ptr++] = v;
    }
    inline T pop_back() throw()
    {
        return data[--ptr];
    }

    inline bool empty() const { return ptr==0; }
    inline void clear() { ptr=0; }
    inline size_t size() const { return ptr; }
    inline size_t capacity() const { return maxsize; }
    inline size_t free() const { return capacity()-ptr; }
    const T& back() const { return data[ptr-1]; }
    const T& front() const { return data[0]; }
    T& back() { return data[ptr-1]; }
    T& front() { return data[0]; }

    void resize(size_t n) throw(SimpleVecException)
    {
        if(n > maxsize) { throw SimpleVecException("resize"); }
        ptr = n;
    }

    void quickswap(SimpleVec& b) throw()
    {
        // Note: after swapping, the two containers' lifetime
        // is entangled. After one destructs, the other must
        // destruct as well.
        std::swap(data, b.data);
        std::swap(ptr, b.ptr);
    }
    void swap(SimpleVec& b) throw()
    {
        std::swap(datacontainer, b.datacontainer);
        quickswap(b);
    }

    void sort() throw()
    {
        std::sort(data,data+ptr);
    }
    void insert(iterator i, const T& value) throw(SimpleVecException)
    {
        if(ptr >= maxsize) { throw SimpleVecException("insert"); }
        iterator endpos = end();
        std::copy_backward(i, endpos, endpos+1);
        ++ptr;
        *i = value;
    }
    void erase(iterator i) throw()
    {
        std::copy(i+1, end(), i);
        --ptr;
    }
    void move(iterator source, iterator destination) throw()
    {
/*

Forward move:

ABcDEFGH
->        move(c,G)
ABDEFcGH

Backward move:

ABCDEfGH
->        move(f,C)
ABfCDEGH

*/
        if(source != destination)
        {
            T tmp = *source;
            if(source < destination)
            {
                std::copy(source+1,destination,source);
                *--destination = tmp;
            }
            else
            {
                std::copy_backward(destination,source,source+1);
                *destination = tmp;
            }
        }
    }
    SimpleVec& operator=(const SimpleVec& b)
    {
        //data = datacontainer;
        std::copy(b.begin(),b.end(), data);
        ptr = b.ptr;
        return *this;
    }
    SimpleVec(const SimpleVec& b) : data(datacontainer), ptr(b.ptr)
    {
        std::copy(b.begin(),b.end(), data);
    }
private:
    T datacontainer[maxsize], *data;
    size_t ptr;
};

template<typename T,size_t maxsize>
class SortedSimpleVec: private std::deque<T>
{
    //typedef SimpleVec<T,maxsize> parent;
    typedef std::deque<T> parent;
public:
    typedef typename parent::iterator iterator;
    typedef typename parent::const_iterator const_iterator;

    using parent::erase;
    using parent::size;
    using parent::begin;
    using parent::clear;
    using parent::end;

    template<typename T2> iterator lower_bound(const T2& value) { return std::lower_bound(begin(),end(), value); }
    template<typename T2> iterator upper_bound(const T2& value) { return std::lower_bound(begin(),end(), value); }
    template<typename T2> const_iterator lower_bound(const T2& value) const { return std::lower_bound(begin(),end(), value); }
    template<typename T2> const_iterator upper_bound(const T2& value) const { return std::lower_bound(begin(),end(), value); }

    void insert(const T& value)
    {
        iterator i = upper_bound(begin(),end(), value);
        parent::insert(i, value);
    }
    void redefine(iterator source, const T& newvalue)
    {
        iterator destination = upper_bound(newvalue);
        *source = newvalue;
        parent::move(source, destination);
    }
    template<typename T2>
    iterator find(const T2& value)
    {
        iterator i = lower_bound(value);
        if(*i != value) return end();
        return i;
    }
    template<typename T2>
    const_iterator find(const T2& value) const
    {
        const_iterator i = lower_bound(value);
        if(*i != value) return end();
        return i;
    }
    template<typename T2>
    T& operator[] (const T2& value)
    {
        iterator i = lower_bound(value);
        if(*i != value)
        {
            size_t pos = i-begin();
            parent::insert(i, T(value));
            i = begin()+pos;
        }
        return *i;
    }
};

#endif
