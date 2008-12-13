#include "longfilewrite.hh"
#include "sparsewrite.hh"
#include "fadvise.hh"

#include <unistd.h>
#include <fcntl.h>

#ifdef USE_LIBAIO
# include <libaio.h>
#endif
#ifdef USE_AIO
# include <aio.h>
#endif

LongFileWrite::LongFileWrite(int fild, uint_fast64_t esize)
    : fd(fild), bufpos(0), expected_size(esize), Buffer()
{
    if(expected_size > 0) FadviseRandom(fd, 0, expected_size);
}

LongFileWrite::LongFileWrite(int fild, uint_fast64_t offset,
                           uint_fast64_t size,
                           const unsigned char* buf,
                           bool use_sparse)
    : fd(fild), bufpos(offset), expected_size(offset+size), Buffer()
{
    write(buf, size, offset, use_sparse);
}


void LongFileWrite::write
    (const unsigned char* buf, uint_fast64_t size, uint_fast64_t offset,
     bool use_sparse)
{
    const unsigned MaxBufSize = std::min(UINT64_C(0x80000), (uint_fast64_t)expected_size);

    //goto JustWriteIt;// DEBUG
    
    if(offset == 0 && size == expected_size) goto JustWriteIt;
    if(Buffer.capacity() != MaxBufSize) Buffer.reserve(MaxBufSize);

    if(offset != getbufend() || Buffer.size() + size > MaxBufSize)
    {
        FlushBuffer();
    }
    
    if(Buffer.empty()) bufpos = offset;
    
    if(offset == getbufend() && Buffer.size() + size <= MaxBufSize)
    {
        /* Append it to the buffer. */
        Buffer.insert(Buffer.end(), buf, buf+size);
    }   
    else
    {
        /* If this data does not fit into the buffer, write it at once. */
    JustWriteIt: // arrived here also if we decided to skip buffering
        if(use_sparse)
        {
            /* Leave holes into the files (allowed by POSIX standard). */
            /* When the block consists entirely of zeroes, it does
             * not need to be written.
             */
            SparseWrite(fd, buf, size, offset);
        }   
        else
            pwrite64(fd, buf, size, offset);
    }
}

void LongFileWrite::FlushBuffer()
{
    if(Buffer.empty()) return;
    pwrite64(fd, &Buffer[0], Buffer.size(), bufpos);
    Buffer.clear();
}

void LongFileWrite::Close()
{
    close(fd);
}

#ifdef USE_LIBAIO
#include <map>
static class io_scheduler /* SCHEDULER USING LIBAIO (KERNEL LEVEL ASYNCHRONOUS I/O) */
{
public:
    io_scheduler()
        : n_pending(0)
    {
        memset(&ctx,0,sizeof(ctx));
        io_setup(2, &ctx);
    }
    ~io_scheduler()
    {
        Flush();
        io_destroy(ctx);
    }
    void Flush()
    {
        GetEvents(n_pending);
    }
    
    void GetEvents(int n_minimum)
    {
        std::vector<io_event> events(n_pending);
        int got = io_getevents(ctx, n_minimum, n_pending, &events[0], NULL);
        if(got > 0)
        {
            n_pending -= got;
            for(int a=0; a<got; ++a)
            {
                io_event& ep = events[a];
                struct iocb* ios = ep.obj;
                
                /* FIXME:
                 * 
                 * For some reason,  this does not work.
                 * Nothing gets written into files.
                 * I'm getting -EINVAL in ep.res.
                 * Randomly, fildes contains zero or the actual fd.
                 * I don't understand why.
                 *
                 */
                
                int fd = ios->aio_fildes;
                fprintf(stderr, "got closing for %d (%ld,%ld)\n", fd, ep.res, ep.res2);
                CheckClose(fd, 1);
            }
        }
    }
    
    void Pwrite(int fd, const unsigned char* buf, uint_fast64_t offset, uint_fast64_t size)
    {
        struct iocb ios;
        void* bufptr = const_cast<void*> ( (const void*)buf );
        fprintf(stderr, "prep pwrite(%d), size=%ld, offset=%ld\n",
            fd, (long)size, (long)offset);
        io_prep_pwrite(&ios, fd, bufptr, size, offset);
    retry: ;
        struct iocb* iosptr = &ios;
        int res = io_submit(ctx, 1, &iosptr);
        if(res == -EAGAIN)
        {
            GetEvents(1);
            goto retry;
        }
        ++n_pending;
        ++fdmap[fd].second;
    }
    
    int Open(const std::string& fn)
    {
        int fd = open(fn.c_str(),
             O_WRONLY | O_LARGEFILE
           | O_DIRECT
    # ifdef O_NOATIME
           | O_NOATIME
    # endif
                  );
        if(fd < 0) throw(errno);
        fdmap[fd] = std::make_pair(false, 0);
        return fd;
    }
    
    void Close(int fd)
    {
        fdmap[fd].first = true;
        CheckClose(fd);
    }

private:
    void CheckClose(int fd, int sub_n = 0)
    {
        std::map<int, std::pair<bool,int> >::iterator i = fdmap.find(fd);
        
        if(i == fdmap.end()) return;
        
        i->second.second -= sub_n;
        
        if(i->second.first == true
        && i->second.second <= 0)
        {
            fdmap.erase(i);
            close(fd);
        }
    }
private:
    io_context_t ctx;
    int n_pending;
    std::map<int/*fd*/, std::pair<bool/*may close*/, int/*n_pending*/> > fdmap;
} scheduler;
#endif


#ifdef USE_AIO
#include <map>
#include <list>
static class io_scheduler /* SCHEDULER USING AIO (USERSPACE LIB) */
{
private:
    typedef std::map<int/*fd*/, std::pair<bool/*may close*/, std::list<struct aiocb64*> > > fdmap_t;
    fdmap_t fdmap;
public:
    io_scheduler() : fdmap()
    {
    }
    ~io_scheduler()
    {
        Flush();
    }
    void Flush()
    {
        for(fdmap_t::iterator j,i = fdmap.begin(); i != fdmap.end(); i=j)
        {
            j=i; ++j;
            CheckClose(i, true);
        }
    }
    
    void Pwrite(int fd, const unsigned char* buf, uint_fast64_t offset, uint_fast64_t size)
    {
        struct aiocb64* newcbp = new aiocb64;
        struct aiocb64& cbp = *newcbp;
        memset(&cbp,0,sizeof(cbp));
        cbp.aio_fildes     = fd;
        cbp.aio_lio_opcode = LIO_WRITE;
        cbp.aio_buf        = const_cast<void*> ( (const void*) buf );
        cbp.aio_nbytes     = size;
        cbp.aio_offset     = offset;
        aio_write64(&cbp);
        fdmap[fd].second.push_back(newcbp);
        CheckClose(fd, false);
    }
    
    int Open(const std::string& fn)
    {
    retry:;
        int fd = open(fn.c_str(),
             O_WRONLY | O_LARGEFILE
    # ifdef O_NOATIME
           | O_NOATIME
    # endif
                  );
        
        if(fd < 0 && (errno == ENFILE || errno == EMFILE))
        {
            Flush();
            goto retry;
        }
        
        if(fd < 0) throw(errno);
        fdmap[fd].first = false;
        return fd;
    }
    
    void Close(int fd)
    {
        fdmap_t::iterator i = fdmap.find(fd);
        if(i != fdmap.end())
        {
            i->second.first = true;
            CheckClose(i, false);
        }
    }

private:
    void CheckClose(int fd, bool WaitClose=false)
    {
        fdmap_t::iterator i = fdmap.find(fd);
        if(i == fdmap.end()) return;
        CheckClose(i, false);
    }        
    void CheckClose(fdmap_t::iterator& i, bool WaitClose)
    {
        typedef std::list<struct aiocb64*> set_t;
        set_t& s = i->second.second;
        for(set_t::iterator k,j = s.begin(); j != s.end(); j=k)
        {
            k=j; ++k;
            
            if(WaitClose)
            {
                if(aio_suspend64(&*j, 1, NULL) == 0)
                {
                    aio_return64(*j);
                    delete *j;
                    s.erase(j);
                }
            }
            else if(aio_error64(*j) != EINPROGRESS)
            {
                aio_return64(*j);
                delete *j;
                s.erase(j);
            }
        }

        if(i->second.first == true
        && i->second.second.empty())
        {
            close(i->first);
            fdmap.erase(i);
        }
    }
} scheduler;
#endif


/* Uses O_NOATIME for some performance gain. If your libc
 * does not support that flag, ignore it.
 */
FileOutputter::FileOutputter(const std::string& target, uint_fast64_t esize)
#if defined(USE_LIBAIO) || defined(USE_AIO)
    : fd(scheduler.Open(target))
#else
    : LongFileWrite(open(target.c_str(), O_WRONLY|O_LARGEFILE
# ifdef O_NOATIME
                                  | O_NOATIME
# endif
                 ), esize)
#endif
{
#if !(defined(USE_LIBAIO) || defined(USE_AIO))
        if(fd < 0) throw(errno);
#endif
}

FileOutputter::~FileOutputter()
{
#if defined(USE_LIBAIO) || defined(USE_AIO)
    scheduler.Close(fd);
#else
    FlushBuffer();
    Close();
#endif
}

#if defined(USE_LIBAIO) || defined(USE_AIO)
void FileOutputter::write
    (const unsigned char* buf, uint_fast64_t size,
     uint_fast64_t offset,
     bool use_sparse)
{
    scheduler.Pwrite(fd, buf, offset, size);
}
#endif


void FileOutputFlushAll()
{
#if defined(USE_LIBAIO) || defined(USE_AIO)
    scheduler.Flush();
#endif
}

