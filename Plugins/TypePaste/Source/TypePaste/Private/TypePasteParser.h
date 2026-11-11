// TypePaste - parser that finds and interprets UENUM/USTRUCT definitions in pasted C++ code

#pragma once

#include "TypePasteTypes.h"

/**
 * Lenient, so pasting a whole AI answer works.
 *  - Markdown code fences (```), prose and #include / #pragma lines are ignored
 *  - Plain enum class / struct without UENUM / USTRUCT macros are recognized
 *  - Statements that cannot be interpreted (functions, constructors, static members ...) are kept verbatim (kept in C++ output)
 */
class FTypePasteParser
{
public:
	static FTPParseResult Parse(const FString& Source);
};
