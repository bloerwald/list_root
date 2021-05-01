#include "lookup3.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <string>

namespace
{
  std::uint64_t hashlittle64 (char const* data, std::size_t size)
  {
    std::uint32_t hashed_low (0);
    std::uint32_t hashed_high (0);
    hashlittle2 (data, size, &hashed_high, &hashed_low);
    return hashed_low | (std::uint64_t (hashed_high) << 32UL);
  }
}

int main (int argc, char** argv)
{
  for (int argi (1); argi < argc; ++argi)
  {
    std::string normalized_wow (argv[argi]);
    std::string normalized_sane (argv[argi]);
    std::transform (normalized_wow.begin(), normalized_wow.end(), normalized_wow.begin(), [] (char c) { return c == '/' ? '\\' : std::toupper (c); });
    std::transform (normalized_sane.begin(), normalized_sane.end(), normalized_sane.begin(), [] (char c) { return c == '\\' ? '/' : std::tolower (c); });
    uint64_t hash (hashlittle64 (normalized_wow.c_str(), normalized_wow.size()));
    std::cout << std::setfill ('0') << std::setw (sizeof(hash) * 2) << std::hex << hash << " " << normalized_sane << "\n";
  }

  return 0;
}
