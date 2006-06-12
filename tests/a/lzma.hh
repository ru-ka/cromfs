#ifndef HHlzmaHH
#define HHlzmaHH

#include <vector>

const std::vector<unsigned char> LZMACompress
    (const std::vector<unsigned char>& buf);

const std::vector<unsigned char> LZMADeCompress
    (const std::vector<unsigned char>& buf);


#endif
