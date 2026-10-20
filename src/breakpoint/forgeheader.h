#pragma once
#include <QByteArray>
#include <QtEndian>

namespace anvil::breakpoint::research {
// Experimental single-FileSet layout, not a claim of engine compatibility.
inline QByteArray singleFileSetRoot(quint32 count) {
    QByteArray root(44, '\0');
    qToLittleEndian(count, root.data());
    qToLittleEndian<quint32>(2, root.data() + 4);
    qToLittleEndian<qint32>(-1, root.data() + 20);
    qToLittleEndian<qint32>(-1, root.data() + 24);
    qToLittleEndian(count, root.data() + 28);
    qToLittleEndian<quint32>(1, root.data() + 32);
    qToLittleEndian<qint64>(1094, root.data() + 36);
    return root;
}
} // namespace anvil::breakpoint::research
