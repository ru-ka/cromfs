#include "../cromfs-defs.hh"
#include <vector>

size_t
    calc_encoded_directory_size(const cromfs_dirinfo& dir);

const std::vector<unsigned char>
    encode_directory(const cromfs_dirinfo& dir);
