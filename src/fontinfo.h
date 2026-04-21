#pragma once

#include <QString>
#include <QVector>

struct FontNameRecord {
    QString label;
    QString value;
};

QVector<FontNameRecord> readFontNameTable(const QString &path);
