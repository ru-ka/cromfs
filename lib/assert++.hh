#ifndef bqtAssertPPhh
#define bqtAssertPPhh
#include <cassert>
#include <ostream>
#include <string>
#include <vector>
#include <map>

/* This is Bisqwit's generic C++ asserts file.
 * The same file is used in many different projects.
 *
 * assert++.hh version 1.0.5.
 */

#include "autoptr"

/* __ASSERT_FUNCTION is defined by glibc. */
#if defined(__ASSERT_FUNCTION) && !defined(NDEBUG)

#include "threadfun.hh"
extern MutexType assert_mutex;

extern bool assertflag;
namespace assertprivate
{
    class asserterbase: public ptrable
    {
    public:
        virtual ~asserterbase() {}
        virtual void dump(std::ostream& ) = 0;
    };
    typedef std::map<std::string, autoptr<asserterbase> > varmapbase;
    class varmap: public varmapbase, public ptrable
    {
    };
    struct assertiondata
    {
        const char *condition;
        const char *file;
        unsigned line;
        const char *func;

        /* These lines are here to make -Weffc++ shut up */
        assertiondata() : condition(),file(),line(),func() { }
        assertiondata(const assertiondata& b)
            : condition(b.condition), file(b.file),line(b.line),func(b.func) { }
        assertiondata& operator=(const assertiondata& b)
        {
            condition=b.condition;
            file     =b.file;
            line     =b.line;
            func     =b.func;
            return *this;
        }
    };
    struct assertion
    {
        assertiondata data;
        autoptr<varmap> vars;

        assertion(): data(),vars() { }
    };
    extern autoptr<varmap> vars;
    extern bool vars_used;
    extern std::vector<assertion> asserttab;

    template<class T>
    class asserter: public asserterbase
    {
        T var;
    public:
        asserter(const T &value) : var(value) {}
        virtual void dump(std::ostream& out) { out << var; }
    };
    class string_asserter: public asserterbase
    {
        std::string var;
    public:
        string_asserter(const std::string &value) : var(value) {}
        virtual void dump(std::ostream& );
    };
    class char_asserter: public asserterbase
    {
        char var;
    public:
        char_asserter(char value) : var(value) {}
        virtual void dump(std::ostream& );
    };
    template<class T>
    void assertvar(const char *name, const T &var)
    {
        if(!vars || vars_used) { vars = new varmap; vars_used = false; }
        (*vars)[name] = new asserter<T> (var);
    }
    void assertvar(const char *name, const std::string &var);
    void assertvar(const char *name, char var);
    void assertvar(const char *name, unsigned char var);
    void assertvar(const char *name, signed char var);
    void flush(const char *file, unsigned line, const char *func);
    void adder(const char *condition, const char *file,
               unsigned line, const char *func);
} /* end namespace */
#undef assert

#define assertbegin() assert_mutex.Lock()

#define assert(condition) (static_cast<void> (\
    (assertprivate::vars_used = true), ((condition) ? 0 : \
        (assertprivate::adder(#condition, __FILE__, __LINE__, __ASSERT_FUNCTION), 0))))
#define assertvar(expr) assertprivate::assertvar(#expr, expr)
#define assertflush() assertprivate::flush(__FILE__, __LINE__, __ASSERT_FUNCTION)
#define assertset() (static_cast<void>(assertflag=false))
#define asserttest() (assertflag)
#else
#define assertbegin() {}
/* If we don't have glibc.. Or have NDEBUG */
#define assertflush() {}
#define assertvar(expr) true
#define assertset()
#define asserttest() (0)
#endif
/* These are shortcuts for defining more vars with one call */
#define assert2var(e1,e2) (assertvar(e1),assertvar(e2))
#define assert3var(e1,e2,e3) (assert2var(e1,e2),assertvar(e3))
#define assert4var(e1,e2,e3,e4) (assert2var(e1,e2),assert2var(e3,e4))
#define assert5var(e1,e2,e3,e4,e5) (assert4var(e1,e2,e3,e4),assertvar(e5))
#define assert6var(e1,e2,e3,e4,e5,e6) (assert4var(e1,e2,e3,e4),assert2var(e5,e6))
#define assert7var(e1,e2,e3,e4,e5,e6,e7) (assert4var(e1,e2,e3,e4),assert3var(e5,e6,e7))
#define assert8var(e1,e2,e3,e4,e5,e6,e7,e8) (assert4var(e1,e2,e3,e4),assert4var(e5,e6,e7,e8))
#define assert9var(e1,e2,e3,e4,e5,e6,e7,e8,e9) (assert8var(e1,e2,e3,e4,e5,e6,e7,e8),assertvar(e9))
#endif
