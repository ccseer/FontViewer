#pragma once

#include <QString>
#include <QtGlobal>

struct UnicodeBlockEntry {
    quint32 start    = 0;
    quint32 end      = 0;
    const char *name = nullptr;
};

namespace unicode_blocks {

// Formats scalar code point as U+XXXX (zero-padded to at least 4 hex digits).
QString formatCodePoint(quint32 cp);

int blockCount();
const UnicodeBlockEntry &blockAt(int index);
int findBlockIndex(quint32 cp);
QString blockName(quint32 cp);

}  // namespace unicode_blocks
