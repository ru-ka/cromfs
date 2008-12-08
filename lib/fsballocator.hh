/*===========================================================================
  This library is released under the MIT license. See FSBAllocator.html
  for further information and documentation.

Copyright (c) 2008 Juha Nieminen

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
=============================================================================*/

#ifndef INCLUDE_FSBALLOCATOR_HH
#define INCLUDE_FSBALLOCATOR_HH

#include <new>
#include <cstdlib>
#include <cassert>
#include <vector>

template<size_t ElemSize>
class FSBAllocator_ElemAllocator
{
    // In 64-bit systems, if 64-bit allocation is desired, change
    // 'size_t' to 'size_t':
    typedef size_t Data_t;

    static const Data_t BlockElements = 1024;

    static const Data_t DSize = sizeof(Data_t);
    static const Data_t ElemSizeInDSize = (ElemSize + (DSize-1)) / DSize;
    static const Data_t UnitSizeInDSize = ElemSizeInDSize + 1;
    static const Data_t BlockSize = BlockElements*UnitSizeInDSize;
    static const Data_t NoData = ~Data_t(0);

    class MemBlock
    {
        Data_t* block;
        Data_t firstFreeUnitIndex, allocatedElementsAmount, endIndex;

     public:
        MemBlock():
            block(0),
            firstFreeUnitIndex(NoData),
            allocatedElementsAmount(0)
        {}

        bool isFull() const
        {
            return allocatedElementsAmount == BlockElements;
        }

        void clear()
        {
            delete[] (char*) block;
            block = 0;
            firstFreeUnitIndex = NoData;
        }

        void* allocate(Data_t vectorIndex)
        {
            ++allocatedElementsAmount;

            if(firstFreeUnitIndex == NoData)
            {
                if(!block)
                {
                    block = (Data_t*) new char[BlockSize*DSize];
                    endIndex = 0;
                }

                Data_t* retval = block + endIndex;
                endIndex += UnitSizeInDSize;
                retval[ElemSizeInDSize] = vectorIndex;
                return retval;
            }
            else
            {
                Data_t* retval = block + firstFreeUnitIndex;
                firstFreeUnitIndex = *retval;
                retval[ElemSizeInDSize] = vectorIndex;
                return retval;
            }
        }

        void deallocate(Data_t* ptr)
        {
            *ptr = firstFreeUnitIndex;
            firstFreeUnitIndex = ptr - block;

            if(--allocatedElementsAmount == 0)
                clear();
        }
    };

    struct BlocksVector
    {
        std::vector<MemBlock> data;

        BlocksVector() { data.reserve(1024); }

        ~BlocksVector()
        {
            for(size_t i = 0; i < data.size(); ++i)
                data[i].clear();
        }
    };

    static BlocksVector blocksVector;
    static std::vector<Data_t> blocksWithFree;

 public:
    static void* allocate()
    {
        if(blocksWithFree.empty())
        {
            blocksWithFree.push_back(blocksVector.data.size());
            blocksVector.data.push_back(MemBlock());
        }

        const Data_t index = blocksWithFree.back();
        MemBlock& block = blocksVector.data[index];
        void* retval = block.allocate(index);

        if(block.isFull())
            blocksWithFree.pop_back();

        return retval;
    }

    static void deallocate(void* ptr)
    {
        if(!ptr) return;

        Data_t* unitPtr = (Data_t*)ptr;
        const Data_t blockIndex = unitPtr[ElemSizeInDSize];
        MemBlock& block = blocksVector.data[blockIndex];

        if(block.isFull()) blocksWithFree.push_back(blockIndex);
        block.deallocate(unitPtr);
    }
};

template<size_t ElemSize>
typename FSBAllocator_ElemAllocator<ElemSize>::BlocksVector
FSBAllocator_ElemAllocator<ElemSize>::blocksVector;

template<size_t ElemSize>
std::vector<typename FSBAllocator_ElemAllocator<ElemSize>::Data_t>
FSBAllocator_ElemAllocator<ElemSize>::blocksWithFree;



template<class Ty>
class FSBAllocator
{
 public:
    typedef size_t size_type;
    typedef ptrdiff_t difference_type;
    typedef Ty *pointer;
    typedef const Ty *const_pointer;
    typedef Ty& reference;
    typedef const Ty& const_reference;
    typedef Ty value_type;

    pointer address(reference val) const { return &val; }
    const_pointer address(const_reference val) const { return &val; }

    template<class Other>
    struct rebind
    {
        typedef FSBAllocator<Other> other;
    };

    FSBAllocator() throw() {}

    template<class Other>
    FSBAllocator(const FSBAllocator<Other>&) throw() {}

    template<class Other>
    FSBAllocator& operator=(const FSBAllocator<Other>&) { return *this; }

    pointer allocate(size_type count, const void* = 0)
    {
        assert(count == 1);
        return static_cast<pointer>
            (FSBAllocator_ElemAllocator<sizeof(Ty)>::allocate());
    }

    void deallocate(pointer ptr, size_type)
    {
        FSBAllocator_ElemAllocator<sizeof(Ty)>::deallocate(ptr);
    }

    void construct(pointer ptr, const Ty& val)
    {
        new ((void *)ptr) Ty(val);
    }

    void destroy(pointer ptr)
    {
        ptr->Ty::~Ty();
    }

    size_type max_size() const throw() { return 1; }
};

#endif
