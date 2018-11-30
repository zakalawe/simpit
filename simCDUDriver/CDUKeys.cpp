#include "CDUKeys.h"

#include <vector>

struct KeyInfo
{
    std::string name;
    char code;
};

static std::vector<KeyInfo> keyData = {
    {"exec", 0},
    {"prog", 0},
    {"hold", 0},
    {"cruise", 0},    
    {"dep-arr", 0},
    {"legs", 0},
    {"menu", 0},
    {"climb", 0},
    {"", 'E'},
    {"", 'D'},
    {"", 'C'},
    {"", 'B'},
    {"", 'A'},
    {"fix", 0},
    {"n1-limit", 0},
    {"route", 0},
    {"", 'J'},
    {"", 'I'},
    {"", 'H'},
    {"", 'G'},
    {"", 'F'},
    {"next-page", 0},
    {"prev-page", 0},
    {"", 0}, // not defined 23
    {"", 'O'},
    {"", 'N'},
    {"", 'M'},
    {"", 'L'},
    {"", 'K'},
    {"", '3'}, 
    {"", '2'}, 
    {"", '1'}, 
    {"", 'T'}, 
    {"", 'S'}, 
    {"", 'R'}, 
    {"", 'Q'}, 
    {"", 'P'}, 
    {"", '6'}, 
    {"", '5'}, 
    {"", '4'}, 
    {"", 'Y'}, 
    {"", 'X'}, 
    {"", 'W'}, 
    {"", 'V'}, 
    {"", 'U'}, 
    {"", '9'}, 
    {"", '8'}, 
    {"", '7'}, 
    {"clear", 0}, 
    {"", '/'}, 
    {"delete", 0}, 
    {"space", ' '}, 
    {"", 'Z'}, 
    {"plus-minus", '+'},
    {"", '0'},
    {"period", '.'},
    {"lsk-L0", 0}, 
    {"lsk-L1", 0}, 
    {"lsk-L2", 0}, 
    {"lsk-L3", 0}, 
    {"lsk-L4", 0}, 
    {"lsk-L5", 0}, 
    {"init-ref", 0}, 
    {"lsk-R0", 0}, 
    {"lsk-R1", 0}, 
    {"lsk-R2", 0}, 
    {"lsk-R3", 0}, 
    {"lsk-R4", 0}, 
    {"lsk-R5", 0}, 
    {"descent", 0}, 
};

char charForKey(Key k)
{
    const auto& info = keyData.at(static_cast<int>(k));
    return info.code;
}

std::string codeForKey(Key k)
{
    const auto& info = keyData.at(static_cast<int>(k));
    if (info.name.empty()) {
        return std::string("char-") + info.code;
    }

    return info.name;
}