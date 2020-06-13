
#include <lookup3.h>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/format.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

static constexpr const std::size_t MD5_HASH_SIZE = 0x10;
using encoding_key = std::array<unsigned char, MD5_HASH_SIZE>;
std::ostream& operator<< (std::ostream& os, encoding_key const& key)
{
  for (int i (key.size()-1); i >= 0 ; --i)
    os << std::setfill ('0') << std::setw (2) << std::hex << ((unsigned int)(key[i]) & 0xff);
  return os;
}

// On-disk version of locale block
struct FILE_LOCALE_BLOCK
{
  uint32_t NumberOfFiles;                        // Number of entries
  uint32_t Flags;
  uint32_t Locales;                              // File locale mask (CASC_LOCALE_XXX)

  // Followed by a block of 32-bit integers (count: NumberOfFiles)
  // Followed by the MD5 and file name hash (count: NumberOfFiles)
};

// On-disk version of root entry
struct FILE_ROOT_ENTRY
{
  encoding_key EncodingKey;                   // MD5 of the file
  uint64_t FileNameHash;                     // Jenkins hash of the file name
};


struct CASC_ROOT_BLOCK
{
  FILE_LOCALE_BLOCK* pLocaleBlockHdr;         // Pointer to the locale block
  uint32_t* FileDataIds;                         // Pointer to the array of File Data IDs
  FILE_ROOT_ENTRY* pRootEntries;
};

// Root file entry for CASC storages without MNDX root file (World of Warcraft 6.0+)
// Does not match to the in-file structure of the root entry
struct rootfile_entry
{
  encoding_key EncodingKey;                       // File encoding key (MD5)
  uint64_t FileNameHash;                         // Jenkins hash of the file name
  uint32_t FileDataId;                               // File Data Index
  uint32_t Locales;                                  // Locale flags of the file
  std::string filename;
  bool is_real_filename;
};

namespace
{
  void print_exception(const std::exception& e)
  {
      std::cerr << "EX: " << e.what();
      try {
          std::rethrow_if_nested(e);
      } catch(const std::exception& e) {
          std::cerr << ": "; print_exception(e);
      } catch(...) {}
  }

  std::unordered_map<uint64_t, std::string> read_listfile (boost::filesystem::path filename)
  {
    std::unordered_map<uint64_t, std::string> hash_to_filename;

    std::ifstream ifs (filename.string());
    if (!ifs)
    {
      throw std::runtime_error
        ( ( boost::format ("could not open file '%1%' for reading: %2%")
          % filename.string()
          % strerror (errno)
          ).str()
        );
    }

    for (std::string line; std::getline (ifs, line); )
    {
      std::string normalized_wow (line);
      std::string normalized_sane (line);
      std::transform (normalized_wow.begin(), normalized_wow.end(), normalized_wow.begin(), [] (char c) { return c == '/' ? '\\' : std::toupper (c); });
      std::transform (normalized_sane.begin(), normalized_sane.end(), normalized_sane.begin(), [] (char c) { return c == '\\' ? '/' : std::tolower (c); });
      uint32_t hashed_low (0);
      uint32_t hashed_high (0);
      hashlittle2 (normalized_wow.c_str(), normalized_wow.size(), &hashed_high, &hashed_low);
      hash_to_filename.emplace (hashed_low | (uint64_t (hashed_high) << 32UL), normalized_sane);
    }
    return hash_to_filename;
  }

  std::vector<char> read_file (boost::filesystem::path path)
  {
    std::ifstream file (path.string(), std::ios::binary | std::ios::ate);
    if (!file)
    {
      throw std::logic_error ("unable to open file " + path.string());
    }
    std::streamsize const size (file.tellg());
    file.seekg (0, std::ios::beg);

    std::vector<char> buffer (size);
    if (!file.read (buffer.data(), size))
    {
      throw std::runtime_error ("failed reading file " + path.string());
    }
    return buffer;
  }

  char const* VerifyLocaleBlock(CASC_ROOT_BLOCK* pBlockInfo, char const* pbFilePointer, char const* pbFileEnd)
  {
      // Validate the file locale block
      pBlockInfo->pLocaleBlockHdr = (FILE_LOCALE_BLOCK*)pbFilePointer;
      pbFilePointer = (char*)(pBlockInfo->pLocaleBlockHdr + 1);
      if(pbFilePointer > pbFileEnd)
          return NULL;

      // Validate the array of 32-bit integers
      pBlockInfo->FileDataIds = (uint32_t*)pbFilePointer;
      pbFilePointer = (char*)(pBlockInfo->FileDataIds + pBlockInfo->pLocaleBlockHdr->NumberOfFiles);
      if(pbFilePointer > pbFileEnd)
          return NULL;

      // Validate the array of root entries
      pBlockInfo->pRootEntries = (FILE_ROOT_ENTRY*)pbFilePointer;
      pbFilePointer = (char*)(pBlockInfo->pRootEntries + pBlockInfo->pLocaleBlockHdr->NumberOfFiles);
      if(pbFilePointer > pbFileEnd)
          return NULL;

      // Return the position of the next block
      return pbFilePointer;
  }

  std::vector<rootfile_entry> read_rootfile ( boost::filesystem::path rootfile
                                            , std::unordered_map<uint64_t, std::string> const& hash_to_filename
                                            )
  {
    //! \todo don't?
    bool const ignore_0x100_flagged (true);
    bool const ignore_0x80_flagged (true);

    std::vector<char> const rootfile_data (read_file (rootfile));

    if(rootfile_data.size() <= sizeof(FILE_LOCALE_BLOCK*))
      throw std::logic_error ("root file smaller than locale block");

    char const* pbRootFile = rootfile_data.data();
    char const* pbRootFileEnd = rootfile_data.data() + rootfile_data.size();

    std::vector<rootfile_entry> FileTable;

    // Now parse the root file
    while (pbRootFile < pbRootFileEnd)
    {
      CASC_ROOT_BLOCK RootBlock;
      // Validate the file locale block
      pbRootFile = VerifyLocaleBlock(&RootBlock, pbRootFile, pbRootFileEnd);
      if(pbRootFile == NULL)
          break;

      // WoW.exe (build 19116): Entries with flag 0x100 set are skipped
      if(RootBlock.pLocaleBlockHdr->Flags & 0x100 && ignore_0x100_flagged)
          continue;

      // WoW.exe (build 19116): Entries with flag 0x80 set are skipped if arg_4 is set to FALSE (which is by default)
      if((RootBlock.pLocaleBlockHdr->Flags & 0x80) && ignore_0x80_flagged)
          continue;

      {
        uint32_t FileDataIndex = 0;

        // WoW.exe (build 19116): Blocks with zero files are skipped
        for(uint32_t i = 0; i < RootBlock.pLocaleBlockHdr->NumberOfFiles; i++)
        {
          rootfile_entry entry;

          // (004147A3) Prepare the rootfile_entry structure
          entry.FileNameHash = RootBlock.pRootEntries[i].FileNameHash;
          entry.FileDataId = FileDataIndex + RootBlock.FileDataIds[i];
          entry.Locales = RootBlock.pLocaleBlockHdr->Locales;
          entry.EncodingKey = RootBlock.pRootEntries[i].EncodingKey;

          auto it (hash_to_filename.find (entry.FileNameHash));
          if (it != hash_to_filename.end())
          {
            std::stringstream stream;
            stream << it->second <<  " __KNOWN__" << std::setfill ('0') << std::setw (sizeof(entry.FileNameHash) * 2) << std::hex << entry.FileNameHash;
            entry.filename = stream.str();
            entry.is_real_filename = true;
          }
          else
          {
            std::stringstream stream;
            stream << "__UNKNOWN__" << std::setfill ('0') << std::setw (sizeof(entry.FileNameHash) * 2) << std::hex << entry.FileNameHash;
            entry.filename = stream.str();
            entry.is_real_filename = false;
          }

          // Move to the next root entry
          FileDataIndex = entry.FileDataId + 1;
          FileTable.emplace_back (std::move (entry));
        }
      }
    }

    std::sort (FileTable.begin(), FileTable.end(), [] (rootfile_entry const& lhs, rootfile_entry const& rhs)
      {
        return std::tie (lhs.FileDataId, lhs.Locales) < std::tie (rhs.FileDataId, rhs.Locales);
      });

    return FileTable;
  }

  std::string nice_locale (uint32_t mask)
  {
    if (mask == 0x1f3f6 || mask == 0xffffffff) return "all";

    std::vector<std::string> locales;
    while (mask)
    {
      #define filter(_mask, _name) if ((mask & _mask) == _mask) { locales.emplace_back (_name); mask &= ~_mask; }

      filter (0x182b0, "eu")
      filter (0x1a2b0, "eur")
      filter (0x00144, "asia")
      filter (0x05002, "amer")
      filter (0x00140, "zhXX")
      filter (0x01080, "esXX")
      filter (0x14000, "ptXX")
      filter (0x00202, "enXX")

      filter (0x00001, "0001")
      filter (0x00002, "enUS")
      filter (0x00004, "koKR")
      filter (0x00008, "0008")
      filter (0x00010, "frFR")
      filter (0x00020, "deDE")
      filter (0x00040, "zhCN")
      filter (0x00080, "esES")
      filter (0x00100, "zhTW")
      filter (0x00200, "enGB")
      filter (0x00400, "0400")
      filter (0x00800, "0800")
      filter (0x01000, "esMX")
      filter (0x02000, "ruRU")
      filter (0x04000, "ptBR")
      filter (0x08000, "itIT")
      filter (0x10000, "ptPT")
      filter (0xfffe0000, "new!")

      #undef filter
    }
    std::string concat;
    for (std::size_t i = 0; i < locales.size() - 1; ++i)
    {
      concat += locales[i] + "+";
    }
    concat += locales.back();
    return concat;
  }
}

int main (int argc, char** argv)
try
{
  if (argc != 4) throw std::runtime_error ("args: listfile rootfile (dump_all|dump_known|dump_unknown|dump_known_names|make_dbc)");

  boost::filesystem::path const listfile (argv[1]);
  boost::filesystem::path const rootfile (argv[2]);
  std::string const mode (argv[3]);

  std::unordered_map<uint64_t, std::string> const hash_to_filename (read_listfile (listfile));
  std::vector<rootfile_entry> const rootfile_entries (read_rootfile (rootfile, hash_to_filename));

  if (mode == "dump_all")
  {
    for (auto const& entry : rootfile_entries)
    {
      std::cout << std::setfill ('0') << std::setw (10) << std::dec << entry.FileDataId << "." << nice_locale (entry.Locales) << " "
                << entry.EncodingKey << " "
                << entry.filename << "\n";
    }
  }
  else if (mode == "dump_known")
  {
    for (auto const& entry : rootfile_entries)
    {
      if (!entry.is_real_filename) continue;
      std::cout << std::setfill ('0') << std::setw (10) << std::dec << entry.FileDataId << "." << nice_locale (entry.Locales) << " "
                << entry.EncodingKey << " "
                << entry.filename << "\n";
    }
  }
  else if (mode == "dump_unknown")
  {
    for (auto const& entry : rootfile_entries)
    {
      if (entry.is_real_filename) continue;
      std::cout << std::setfill ('0') << std::setw (10) << std::dec << entry.FileDataId << "." << nice_locale (entry.Locales) << " "
                << entry.EncodingKey << " "
                << entry.filename << "\n";
    }
  }
  else if (mode == "dump_known_names")
  {
    for (auto const& entry : rootfile_entries)
    {
      if (!entry.is_real_filename) continue;
      std::cout << entry.filename << "\n";
    }
  }
  else if (mode == "make_dbc")
  {
    struct FileDataRec
    {
      uint32_t m_ID;
      uint32_t m_filename;
      uint32_t m_filepath;
      FileDataRec (uint32_t id, uint32_t name, uint32_t path) : m_ID (id), m_filename (name), m_filepath (path) {}
    };

    std::vector<FileDataRec> recs;

    std::vector<char> stringblock;
    std::map<std::string, std::size_t> stringblock_entries;

    auto add_to_stringblock ([&] (std::string const& s)
    {
      if (!stringblock_entries.count (s))
      {
        stringblock_entries.emplace (s, stringblock.size());
        stringblock.insert (stringblock.end(), s.begin(), s.end());
        stringblock.push_back ('\0');
      }

      return stringblock_entries.at (s);
    });

    for (auto const& entry : rootfile_entries)
    {
      if (!entry.is_real_filename) continue;

      boost::filesystem::path p (entry.filename);
      auto dir (add_to_stringblock (boost::replace_all_copy<std::string> (boost::to_upper_copy<std::string>( (p.parent_path().string() + "/")), "/", "\\")));
      auto fn (add_to_stringblock (boost::to_upper_copy<std::string>(p.filename().string())));
      recs.emplace_back (entry.FileDataId, fn, dir);
    }

    FILE* out_dbc (fopen ("filedata.dbc", "w+"));
    { uint32_t v (1128416343); fwrite (&v, sizeof (v), 1, out_dbc); }
    { uint32_t v (recs.size()); fwrite (&v, sizeof (v), 1, out_dbc); }
    { uint32_t v (3); fwrite (&v, sizeof (v), 1, out_dbc); }
    { uint32_t v (12); fwrite (&v, sizeof (v), 1, out_dbc); }
    { uint32_t v (stringblock.size()); fwrite (&v, sizeof (v), 1, out_dbc); }
    fwrite (recs.data(), sizeof (FileDataRec), recs.size(), out_dbc);
    fwrite (stringblock.data(), stringblock.size(), 1, out_dbc);
    fclose (out_dbc);
  }
  else
  {
    throw std::runtime_error ("unknown mode " + mode);
  }

  return 0;
}
catch (std::exception const& ex)
{
  print_exception (ex);
  std::cerr << "\n";
  return 1;
}
