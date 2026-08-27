#include "FrensHelpers.h"

namespace Frens {
    // string helper functions
    //
    // test if string ends with suffix
    //
    bool endsWith(std::string const &str, std::string const &suffix)
    {
        if (str.length() < suffix.length())
        {
            return false;
        }
        return str.compare(str.length() - suffix.length(), suffix.length(), suffix) == 0;
    }
    //
    // returns lowercase of string s
    //
    std::string str_tolower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c)
                       { return std::tolower(c); } // correct
        );
        return s;
    }

    // Case insensitive: this is used to match file extensions, and a ROM named
    // .GG or .Gg is the same thing as one named .gg. Matching case sensitively
    // meant such a file was neither listed by the browser nor recognised as a
    // Game Gear cartridge.
    bool cstr_endswith(const char *string, const char *width) {
        int lstring = strlen(string);
        int wlen = strlen(width);
        if ( wlen >= lstring ) {
            return false;
        }
        int pos = lstring - wlen;
        return( strcasecmp(string + pos, width) == 0);
    }
}