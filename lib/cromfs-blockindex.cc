#include "cromfs-blockindex.hh"

block_index_type::block_index_type(const block_index_type& b)
    : autoindex(b.autoindex), realindex(b.realindex),
      last_search_a(), last_search_r()
{
}

block_index_type& block_index_type::operator=(const block_index_type& b)
{
    if(&b != this)
    {
        last_search_a.reset();
        last_search_r.reset();
        autoindex = b.autoindex;
        realindex = b.realindex;
    }
    return *this;
}
