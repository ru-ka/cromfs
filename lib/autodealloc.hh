#ifndef bqtAutoDeAllocHH
#define bqtAutoDeAllocHH

template<typename T>
class autodealloc_array
{
    T* p;
public:
    autodealloc_array(T*q): p(q) { }
    ~autodealloc_array() { delete[] p; }
private:
    autodealloc_array(const autodealloc_array<T>&);
    void operator=(const autodealloc_array<T>&);
};

template<typename T>
class autodealloc
{
    T* p;
public:
    autodealloc(T*q): p(q) { }
    ~autodealloc() { delete p; }
private:
    autodealloc(const autodealloc<T>&);
    void operator=(const autodealloc<T>&);
};

#endif
