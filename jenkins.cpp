#include "lookup3.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <string>

int main (int argc, char** argv)
{
  for (int argi (1); argi < argc; ++argi)
  {
    std::string normalized_wow (argv[argi]);
    std::string normalized_sane (argv[argi]);
    std::transform (normalized_wow.begin(), normalized_wow.end(), normalized_wow.begin(), [] (char c) { return c == '/' ? '\\' : std::toupper (c); });
    std::transform (normalized_sane.begin(), normalized_sane.end(), normalized_sane.begin(), [] (char c) { return c == '\\' ? '/' : std::tolower (c); });
    uint64_t hash (hashlittle2 (normalized_wow.c_str(), normalized_wow.size()));
    std::cout << std::setfill ('0') << std::setw (sizeof(hash) * 2) << std::hex << hash << " " << normalized_sane << "\n";
  }

  return 0;
}
