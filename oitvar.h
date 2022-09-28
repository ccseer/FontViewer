#ifndef OITVAR_H
#define OITVAR_H

#include <QByteArray>
#include <QDataStream>

// send to seer
#define SEER_OIT_MSG 6000
#define SEER_OIT_SUB_LOAD_ERR 6001
#define SEER_OIT_SUB_LOAD_OK 6002

#define SEER_OIT_SUB_KEY_PRESS 6003

// send from seer
#define SEER_OIT_DPI_CHANGED 6100
#define SEER_OIT_SIZE_CHANGED 6101
#define SEER_OIT_ATTACHED 6102
#define SEER_OIT_THEME_CHANGED 6103

// exe return
#define ERR_BAD_ARG -1
#define ERR_FILE_NOT_FOUND -2
#define ERR_PROCESS -3

struct OITData {
    // sub_type -> SEER_OIT_SUB_*
    int sub_type  = 0;
    int wnd_index = 0;
    QByteArray data;
};
inline QDataStream& operator<<(QDataStream& out, const OITData& v)
{
    out << v.sub_type << v.wnd_index << v.data;
    return out;
}
inline QDataStream& operator>>(QDataStream& in, OITData& v)
{
    in >> v.sub_type;
    in >> v.wnd_index;
    in >> v.data;
    return in;
}

#endif  // OITVAR_H
