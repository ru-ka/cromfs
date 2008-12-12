#include "assert++.hh"

#include <cstdlib>
#include <cctype>
#include <set>
#include <list>

#undef assertvar

/* This is Bisqwit's generic C++ asserts file.
 * The same file is used in many different projects.
 *
 * assert++.cc version 1.0.5.
 */

bool assertflag;

struct parec
{
    std::string var;
    std::string::size_type pos;
    char begin, end;

    parec(): var(),pos(),begin(),end() { }
};

namespace assertprivate
{
    std::vector<assertion> asserttab;
    autoptr<varmap> vars;
    bool vars_used = false;

    void adder(const char *condition, const char *file,
               unsigned line, const char *func)
    {
        assertflag = true;
        assertion a;
        a.data.condition = condition;
        a.data.file = file;
        a.data.line = line;
        a.data.func = func;
        a.vars = vars;
        asserttab.push_back(a);
    }

    void string_asserter::dump()
    {
        std::cerr << '"';
        for(unsigned a=0; a<var.size(); ++a)
            if(var[a]=='\n')std::cerr << "\\n";
            else if(var[a]=='\r')std::cerr << "\\r";
            else if(var[a]=='\t')std::cerr << "\\t";
            else if(var[a]=='\\')std::cerr << "\\\\";
            else std::cerr << var[a];
        std::cerr << '"';
    }

    void char_asserter::dump()
    {
        std::cerr << '\'';
        if(var=='\n')std::cerr << "\\n";
        else if(var=='\r')std::cerr << "\\r";
        else if(var=='\t')std::cerr << "\\t";
        else if(var=='\\')std::cerr << "\\\\";
        else std::cerr << var;
        std::cerr << '\'';
    }

    void assertvar(const char *name, const std::string &var)
    {
        if(!vars || vars_used) { vars = new varmap; vars_used = false; }
        (*vars)[name] = new string_asserter (var);
    }
    void assertvar(const char *name, char var)
    {
        if(!vars || vars_used) { vars = new varmap; vars_used = false; }
        (*vars)[name] = new char_asserter (var);
    }
    void assertvar(const char *name, unsigned char var) { assertvar(name, (unsigned)var); }
    void assertvar(const char *name, signed char var) { assertvar(name, (int)var); }

    void flush(const char *file, unsigned line, const char *func)
    {
        vars_used = true;
        if(asserttab.size())
        {
            std::cerr << file << '[' << func << "]:" << line << ":Failing assertions:\n";
            typedef std::map<varmap*, std::vector<assertiondata> > tabtype;
            tabtype tab;
            for(unsigned i=0; i<asserttab.size(); ++i)
            {
                assertion &a = asserttab[i];
                tab[a.vars].push_back(a.data);
            }

            for(tabtype::const_iterator i=tab.begin(); i!=tab.end(); ++i)
            {
                typedef std::set<std::string> varset;
                varset vars;
                for(std::string::size_type j=0; j<i->second.size(); ++j)
                {
                    const assertiondata &a = i->second[j];
                    const std::string &condition = a.condition;
                    std::cerr << a.file << '[' << a.func << "]:" << a.line
                              << ": " << condition << std::endl;

                    parec state;
                    state.var = "";
                    std::list<parec> stack;
                    enum { main, str, strquo, chr, chrquo } op = main;
                    for(std::string::size_type a=0; a<condition.size(); ++a)
                    {
                        switch(op)
                        {
                            case main:
                            {
                                if(condition[a] == '"') { op = str; continue; }
                                if(condition[a] == '\'') { op = chr; continue; }
                                break;
                            }
                            case str:
                            {
                                if(state.var.size())
                                {
                                    vars.insert(state.var);
                                    state.var = "";
                                }
                                if(condition[a] == '"') { op = main; continue; }
                                if(condition[a] == '\\') { op = strquo; continue; }
                                continue;
                            }
                            case strquo:
                            {
                                op = str;
                                continue;
                            }
                            case chr:
                            {
                                if(state.var.size())
                                {
                                    vars.insert(state.var);
                                    state.var = "";
                                }
                                if(condition[a] == '\'') { op = main; continue; }
                                if(condition[a] == '\\') { op = chrquo; continue; }
                                continue;
                            }
                            case chrquo:
                            {
                                op = chr;
                                continue;
                            }
                        }
                        if(std::isalpha(condition[a])
                        || condition[a]=='_'
                        || (state.var.size() && std::isdigit(condition[a]))
                        || condition[a]=='.')
                        {
                            if(condition[a]=='.' && state.var.size())
                                vars.insert(state.var);
                            state.var += condition[a];
                            continue;
                        }
                        if(condition[a]=='[')
                        {
                            if(state.var.size())vars.insert(state.var);
                            state.begin = condition[a];
                            state.pos = a+1;
                            state.end = ']';
                            stack.push_front(state);
                            state.var = "";
                        }
                        else if(condition[a]=='(')
                        {
                            if(state.var.size())vars.insert(state.var);
                            state.begin = condition[a];
                            state.pos = a+1;
                            state.end = ')';
                            stack.push_front(state);
                            state.var = "";
                        }
                        else if(condition[a]=='{')
                        {
                            if(state.var.size())vars.insert(state.var);
                            state.begin = condition[a];
                            state.pos = a+1;
                            state.end = '}';
                            stack.push_front(state);
                            state.var = "";
                        }
                        else
                        {
                            if(state.var.size())
                            {
                                vars.insert(state.var);
                                state.var = "";
                            }
                            std::list<parec>::iterator i = stack.begin();
                            if(i != stack.end() && condition[a] == i->end)
                            {
                                std::string expr = condition.substr(i->pos, a - i->pos);
                                /*if(expr.size())
                                    vars.insert(expr);*/
                                state = *i;
                                if(state.var.size())
                                {
                                    state.var += state.begin;
                                    state.var += expr;
                                    state.var += state.end;
                                }
                                stack.erase(stack.begin());
                            }
                        }
                    }
                    if(state.var.size()) vars.insert(state.var);
                }
                /* Dump the vars found in the expressions
                for(varset::const_iterator k = vars.begin(); k!=vars.end(); ++k)
                    std::cerr << "Var: " << *k << std::endl;*/
                bool first=true;
                const varmapbase *mappi = i->first;
                for(varmapbase::const_iterator j=mappi->begin(); j!=mappi->end(); ++j)
                {
                    const std::string &s = j->first;

                    /* Don't display the variable if it isn't
                     * used in any of the expressions
                     */
                    varset::const_iterator k = vars.find(s);
                    if(k == vars.end())continue;

                    if(first) { first=false;
                         std::cerr <<    "\t\t(With ";}
                    else std::cerr << ", ";
                    std::cerr << s << '=';
                    j->second->dump();
                }
                if(!first)std::cerr << ")\n";
            }
            std::exit(-1);
        }
    }
}
