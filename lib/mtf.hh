#include <vector>

/* Move-to-front transform. */

const std::vector<unsigned char>
    MTF_encode(const unsigned char* data, const size_t size);

const std::vector<unsigned char>
    MTF_decode(const unsigned char* data, const size_t size);

static inline const std::vector<unsigned char>
    MTF_encode(const std::vector<unsigned char>& input)
{
    return MTF_encode(&input[0], input.size());
}
static inline const std::vector<unsigned char>
    MTF_decode(const std::vector<unsigned char>& input)
{
    return MTF_decode(&input[0], input.size());
}
