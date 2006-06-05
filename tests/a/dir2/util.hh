#include <string>
#include <stdint.h>

const std::string ReportSize(uint_fast64_t size);

bool is_zero_block(const unsigned char* data, uint_fast64_t size);

const std::string TranslateMode(unsigned mode);

