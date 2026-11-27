#include "charactergridmodel.h"

#include <QRegularExpression>
#include <QtMath>

#include <algorithm>

#include "engine/unicodeblocks.h"

namespace {
struct ParsedQuery {
    enum class Mode {
        None,
        Range,
        HexPrefix,
        SingleScalar,
        BlockSubstring,
    };

    Mode mode          = Mode::None;
    quint32 range_lo   = 0;
    quint32 range_hi   = 0;
    quint32 exact_cp   = 0;
    quint32 sibling_cp = 0;
    bool explicit_hex  = false;
    QString hex_prefix;
    char hex_ascii[8]{};
    int hex_ascii_len  = 0;
    QString text;
};

void assignHexPrefix(ParsedQuery &pq, const QString &upper_hex)
{
    pq.hex_prefix    = upper_hex;
    pq.hex_ascii_len = qMin(upper_hex.size(), 8);
    for (int i = 0; i < pq.hex_ascii_len; ++i) {
        pq.hex_ascii[i] = char(upper_hex.at(i).unicode());
    }
}

bool isHexString(const QString &s)
{
    if (s.isEmpty()) {
        return false;
    }
    for (QChar c : s) {
        const ushort u = c.unicode();
        const bool ok  = (u >= '0' && u <= '9') || (u >= 'a' && u <= 'f')
                        || (u >= 'A' && u <= 'F');
        if (!ok) {
            return false;
        }
    }
    return true;
}

ParsedQuery parseQuery(const QString &raw)
{
    ParsedQuery pq;
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty()) {
        return pq;
    }

    // 1) Range syntax: e.g. U+4E00-U+4E20, 0x4E00..0x4E20, 4e00-4eff
    static const QRegularExpression range_re(QStringLiteral(
        "^(?:U\\+|0X)?([0-9A-F]{1,6})\\s*(?:-|–|\\.\\.)\\s*(?:U\\+|0X)?([0-9A-"
        "F]{1,6})$"),
        QRegularExpression::CaseInsensitiveOption);
    if (const QRegularExpressionMatch rm = range_re.match(trimmed);
        rm.hasMatch()) {
        bool ok1 = false;
        bool ok2 = false;
        const quint32 a = rm.captured(1).toUInt(&ok1, 16);
        const quint32 b = rm.captured(2).toUInt(&ok2, 16);
        if (ok1 && ok2 && a <= 0x10FFFFu && b <= 0x10FFFFu) {
            pq.mode     = ParsedQuery::Mode::Range;
            pq.range_lo = qMin(a, b);
            pq.range_hi = qMax(a, b);
            return pq;
        }
    }

    // 2) Explicit U+ / 0x hex prefix
    if (trimmed.startsWith(QLatin1String("U+"), Qt::CaseInsensitive)
        || trimmed.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
        const QString hex = trimmed.mid(2).trimmed().toUpper();
        if (hex.size() >= 1 && hex.size() <= 6 && isHexString(hex)) {
            bool ok         = false;
            const quint32 v = hex.toUInt(&ok, 16);
            if (ok && v <= 0x10FFFFu) {
                pq.mode         = ParsedQuery::Mode::HexPrefix;
                pq.explicit_hex = true;
                pq.exact_cp     = v;
                assignHexPrefix(pq, hex);
                return pq;
            }
        }
    }

    // 3) Single Unicode scalar (including SMP surrogate pair)
    const QVector<uint> ucs4 = trimmed.toUcs4();
    if (ucs4.size() == 1) {
        pq.mode     = ParsedQuery::Mode::SingleScalar;
        pq.exact_cp = ucs4.at(0);
        if (pq.exact_cp >= 'A' && pq.exact_cp <= 'Z') {
            pq.sibling_cp = pq.exact_cp + ('a' - 'A');
        }
        else if (pq.exact_cp >= 'a' && pq.exact_cp <= 'z') {
            pq.sibling_cp = pq.exact_cp - ('a' - 'A');
        }
        return pq;
    }

    // 4) Pure 2..6 hex digits -> code-point prefix/exact match only (avoid noisy
    //    block substring hits such as "ab" matching "Arabic").
    if (trimmed.size() >= 2 && trimmed.size() <= 6 && isHexString(trimmed)) {
        bool ok         = false;
        const quint32 v = trimmed.toUInt(&ok, 16);
        if (ok && v <= 0x10FFFFu) {
            pq.mode         = ParsedQuery::Mode::HexPrefix;
            pq.explicit_hex = false;
            pq.exact_cp     = v;
            pq.text         = trimmed;
            assignHexPrefix(pq, trimmed.toUpper());
            return pq;
        }
    }

    // 5) Block name substring
    pq.mode = ParsedQuery::Mode::BlockSubstring;
    pq.text = trimmed;
    return pq;
}

bool matchesHexPrefix(quint32 cp, const ParsedQuery &pq)
{
    if (cp == pq.exact_cp) {
        return true;
    }
    char unpadded[8];
    int ulen  = 0;
    quint32 v = cp;
    do {
        const quint32 nib = v & 0xFu;
        unpadded[ulen++]  = char(nib < 10 ? ('0' + nib) : ('A' + (nib - 10)));
        v >>= 4;
    } while (v != 0 && ulen < 8);
    for (int i = 0; i < ulen / 2; ++i) {
        const char tmp         = unpadded[i];
        unpadded[i]            = unpadded[ulen - 1 - i];
        unpadded[ulen - 1 - i] = tmp;
    }

    const int qlen  = pq.hex_ascii_len;
    auto startsWith = [&](const char *buf, int len) {
        if (len < qlen) {
            return false;
        }
        for (int i = 0; i < qlen; ++i) {
            if (buf[i] != pq.hex_ascii[i]) {
                return false;
            }
        }
        return true;
    };
    if (startsWith(unpadded, ulen)) {
        return true;
    }
    if (ulen < 4) {
        char padded[4] = {'0', '0', '0', '0'};
        for (int i = 0; i < ulen; ++i) {
            padded[4 - ulen + i] = unpadded[i];
        }
        if (startsWith(padded, 4)) {
            return true;
        }
    }
    return false;
}

bool matchesQuery(quint32 cp, const ParsedQuery &pq)
{
    switch (pq.mode) {
    case ParsedQuery::Mode::None:
        return true;
    case ParsedQuery::Mode::Range:
        return cp >= pq.range_lo && cp <= pq.range_hi;
    case ParsedQuery::Mode::HexPrefix:
        return matchesHexPrefix(cp, pq);
    case ParsedQuery::Mode::SingleScalar:
        return cp == pq.exact_cp
               || (pq.sibling_cp != 0 && cp == pq.sibling_cp);
    case ParsedQuery::Mode::BlockSubstring:
        return unicode_blocks::blockName(cp).contains(pq.text,
                                                      Qt::CaseInsensitive);
    }
    return true;
}
}  // namespace

void CharacterGridModel::setAllCodePoints(
    const QVector<quint32> &code_points,
    const QVector<quint32> &glyph_ids,
    const QVector<QString> &glyph_names)
{
    const bool has_gids  = (glyph_ids.size() == code_points.size());
    const bool has_names = (glyph_names.size() == code_points.size());
    if (std::is_sorted(code_points.cbegin(), code_points.cend())) {
        m_all         = code_points;
        m_sort_order.clear();
        m_glyph_ids   = has_gids ? glyph_ids : QVector<quint32>{};
        m_glyph_names = has_names ? glyph_names : QVector<QString>{};
    }
    else {
        QVector<int> order(code_points.size());
        for (int i = 0; i < order.size(); ++i) {
            order[i] = i;
        }
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            return code_points.at(a) < code_points.at(b);
        });
        m_all.clear();
        m_all.reserve(order.size());
        m_glyph_ids.clear();
        if (has_gids) {
            m_glyph_ids.reserve(order.size());
        }
        m_glyph_names.clear();
        if (has_names) {
            m_glyph_names.reserve(order.size());
        }
        for (int idx : order) {
            m_all.append(code_points.at(idx));
            if (has_gids) {
                m_glyph_ids.append(glyph_ids.at(idx));
            }
            if (has_names) {
                m_glyph_names.append(glyph_names.at(idx));
            }
        }
        m_sort_order = std::move(order);
    }
    m_block_filter = kAllBlocks;
    rebuildBuckets();
    applyFilter();
}

void CharacterGridModel::setGlyphNames(const QVector<QString> &glyph_names)
{
    if (glyph_names.size() != m_all.size()) {
        if (m_glyph_names.isEmpty()) {
            return;
        }
        m_glyph_names.clear();
    }
    else if (m_sort_order.isEmpty()) {
        m_glyph_names = glyph_names;
    }
    else {
        m_glyph_names.clear();
        m_glyph_names.reserve(m_sort_order.size());
        for (int idx : m_sort_order) {
            m_glyph_names.append(glyph_names.at(idx));
        }
    }
    if (!m_query.isEmpty()) {
        applyFilter();
    }
}

quint32 CharacterGridModel::glyphIdForCodePoint(quint32 cp) const
{
    if (cp == 0 || m_glyph_ids.isEmpty()) {
        return 0;
    }
    auto it = std::lower_bound(m_all.cbegin(), m_all.cend(), cp);
    if (it != m_all.cend() && *it == cp) {
        const int idx = int(it - m_all.cbegin());
        return idx < m_glyph_ids.size() ? m_glyph_ids.at(idx) : 0u;
    }
    return 0;
}

QString CharacterGridModel::glyphNameForCodePoint(quint32 cp) const
{
    if (cp == 0 || m_glyph_names.isEmpty()) {
        return {};
    }
    auto it = std::lower_bound(m_all.cbegin(), m_all.cend(), cp);
    if (it != m_all.cend() && *it == cp) {
        const int idx = int(it - m_all.cbegin());
        return idx < m_glyph_names.size() ? m_glyph_names.at(idx) : QString();
    }
    return {};
}

bool CharacterGridModel::setBlockFilter(int block_index)
{
    if (m_block_filter == block_index) {
        return false;
    }
    m_block_filter = block_index;
    applyFilter();
    return true;
}

bool CharacterGridModel::setSearchQuery(const QString &query)
{
    if (m_query == query) {
        return false;
    }
    m_query = query;
    applyFilter();
    return true;
}

bool CharacterGridModel::containsCodePoint(quint32 cp) const
{
    if (cp == 0) {
        return false;
    }
    return std::binary_search(m_filtered.cbegin(), m_filtered.cend(), cp);
}

int CharacterGridModel::calcColumns(int avail_w, int cell_px, int pad_x)
{
    if (cell_px <= 0) {
        return 1;
    }
    const int inner_w = qMax(cell_px, avail_w - pad_x * 2);
    return qMax(1, inner_w / cell_px);
}

int CharacterGridModel::calcRows(int total_count, int cols)
{
    if (total_count <= 0 || cols <= 0) {
        return 0;
    }
    return (total_count + cols - 1) / cols;
}

void CharacterGridModel::rebuildBuckets()
{
    m_buckets.clear();
    const int total_blocks = unicode_blocks::blockCount();
    QVector<int> counts(total_blocks, 0);
    int unassigned_count = 0;

    // Code points are strictly ascending, so one linear pass with a block
    // cursor counts all blocks in O(N + B).
    int block_idx = 0;
    for (quint32 cp : m_all) {
        while (block_idx < total_blocks
               && cp > unicode_blocks::blockAt(block_idx).end) {
            ++block_idx;
        }
        if (block_idx < total_blocks
            && cp >= unicode_blocks::blockAt(block_idx).start) {
            ++counts[block_idx];
        }
        else {
            ++unassigned_count;
        }
    }

    m_buckets.append(
        {kAllBlocks, QStringLiteral("All Characters"), int(m_all.size())});
    for (int i = 0; i < total_blocks; ++i) {
        if (counts[i] > 0) {
            m_buckets.append(
                {i, QString::fromLatin1(unicode_blocks::blockAt(i).name),
                 counts[i]});
        }
    }
    if (unassigned_count > 0) {
        m_buckets.append(
            {kUnassignedBlock, QStringLiteral("Unassigned / Other"),
             unassigned_count});
    }
}

void CharacterGridModel::applyFilter()
{
    const ParsedQuery pq = parseQuery(m_query);
    if (m_block_filter == kAllBlocks && pq.mode == ParsedQuery::Mode::None) {
        m_filtered = m_all;
        return;
    }

    m_filtered.clear();
    m_filtered.reserve(m_all.size());

    const int block_count = unicode_blocks::blockCount();
    const bool has_valid_block
        = (m_block_filter >= 0 && m_block_filter < block_count);
    const UnicodeBlockEntry block_entry
        = has_valid_block ? unicode_blocks::blockAt(m_block_filter)
                          : UnicodeBlockEntry{};

    QVector<quint8> matched_blocks;
    int query_block_cursor      = 0;
    int unassigned_block_cursor = 0;
    if (pq.mode == ParsedQuery::Mode::BlockSubstring) {
        matched_blocks.resize(block_count);
        for (int b = 0; b < block_count; ++b) {
            matched_blocks[b]
                = QString::fromLatin1(unicode_blocks::blockAt(b).name)
                          .contains(pq.text, Qt::CaseInsensitive)
                      ? 1
                      : 0;
        }
    }

    for (int i = 0; i < m_all.size(); ++i) {
        const quint32 cp = m_all.at(i);
        if (has_valid_block) {
            if (cp < block_entry.start || cp > block_entry.end) {
                continue;
            }
        }
        else if (m_block_filter == kUnassignedBlock) {
            while (unassigned_block_cursor < block_count
                   && cp > unicode_blocks::blockAt(unassigned_block_cursor).end) {
                ++unassigned_block_cursor;
            }
            if (unassigned_block_cursor < block_count
                && cp >= unicode_blocks::blockAt(unassigned_block_cursor).start) {
                continue;
            }
        }

        if (pq.mode == ParsedQuery::Mode::BlockSubstring) {
            while (query_block_cursor < block_count
                   && cp > unicode_blocks::blockAt(query_block_cursor).end) {
                ++query_block_cursor;
            }
            bool matched = false;
            if (query_block_cursor < block_count
                && cp >= unicode_blocks::blockAt(query_block_cursor).start) {
                matched = (matched_blocks[query_block_cursor] != 0);
            }
            else {
                matched = unicode_blocks::blockName(cp).contains(
                    pq.text, Qt::CaseInsensitive);
            }
            if (!matched && i < m_glyph_names.size()
                && !m_glyph_names.at(i).isEmpty()) {
                matched = m_glyph_names.at(i).contains(pq.text,
                                                       Qt::CaseInsensitive);
            }
            if (matched) {
                m_filtered.append(cp);
            }
            continue;
        }

        bool matched = matchesQuery(cp, pq);
        if (!matched && pq.mode == ParsedQuery::Mode::HexPrefix
            && !pq.explicit_hex && i < m_glyph_names.size()
            && !m_glyph_names.at(i).isEmpty()) {
            matched = (m_glyph_names.at(i).compare(pq.text, Qt::CaseInsensitive)
                       == 0);
        }
        if (matched) {
            m_filtered.append(cp);
        }
    }
}
