#pragma once

#include "mm/Types.h"
#include <map>
#include <string>

/** Built-in phrases (original material), shown under "Factory" in the library. */
namespace mmp
{
    const std::map<std::string, mm::Phrase>& factoryPhrases();
    mm::Phrase defaultPhrase();
}
