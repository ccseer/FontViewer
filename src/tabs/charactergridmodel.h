#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>

class CharacterGridModel {
public:
    static constexpr int kAllBlocks        = -2;
    static constexpr int kUnassignedBlock  = -1;

    struct BlockBucket {
        int block_index = kAllBlocks;
        QString name;
        int count = 0;
    };

    void setAllCodePoints(const QVector<quint32> &code_points,
                          const QVector<quint32> &glyph_ids   = {},
                          const QVector<QString> &glyph_names = {});
    void setGlyphNames(const QVector<QString> &glyph_names);
    quint32 glyphIdForCodePoint(quint32 cp) const;
    QString glyphNameForCodePoint(quint32 cp) const;

    int totalCount() const
    {
        return m_all.size();
    }
    const QVector<quint32> &allCodePoints() const
    {
        return m_all;
    }
    const QVector<BlockBucket> &blockBuckets() const
    {
        return m_buckets;
    }

    bool setBlockFilter(int block_index);
    int blockFilter() const
    {
        return m_block_filter;
    }

    bool setSearchQuery(const QString &query);
    const QString &searchQuery() const
    {
        return m_query;
    }

    const QVector<quint32> &filteredCodePoints() const
    {
        return m_filtered;
    }
    int filteredCount() const
    {
        return m_filtered.size();
    }
    bool containsCodePoint(quint32 cp) const;

    static int calcColumns(int avail_w, int cell_px, int pad_x);
    static int calcRows(int total_count, int cols);

private:
    void rebuildBuckets();
    void applyFilter();

    QVector<quint32> m_all;
    QVector<quint32> m_glyph_ids;
    QVector<QString> m_glyph_names;
    QVector<int> m_sort_order;
    QVector<BlockBucket> m_buckets;
    QVector<quint32> m_filtered;
    int m_block_filter = kAllBlocks;
    QString m_query;
};
