#include <vector>

/* Burrows-Wheeler transform. */

const std::vector<unsigned char>
    BWT_encode(const unsigned char* data, size_t size, size_t& primary_index);

const std::vector<unsigned char>
    BWT_decode(const unsigned char* data, size_t size, const size_t primary_index);


const std::vector<unsigned char>
    BWT_encode(const std::vector<unsigned char>& input, size_t& primary_index);

const std::vector<unsigned char>
    BWT_decode(const std::vector<unsigned char>& input, const size_t primary_index);


const std::vector<unsigned char>
    BWT_encode_embedindex(const unsigned char* data, size_t size);

const std::vector<unsigned char>
    BWT_decode_embedindex(const unsigned char* data, size_t size);


const std::vector<unsigned char>
    BWT_encode_embedindex(const std::vector<unsigned char>& input);

const std::vector<unsigned char>
    BWT_decode_embedindex(const std::vector<unsigned char>& input);
