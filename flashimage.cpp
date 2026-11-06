#include "flashimage.h"
#include <QCryptographicHash>

FlashImage::FlashImage()
    : m_base(0), m_size(0), m_writtenBytes(0), m_file(nullptr)
{
}

FlashImage::~FlashImage()
{
    close();
}

void FlashImage::configure(quint32 base, quint32 size, const QString &path)
{
    close();
    m_base = base;
    m_size = size;
    m_path = path;
    m_writtenBytes = 0;
    m_erasedRanges.clear();
}

bool FlashImage::open()
{
    close();

    if (m_size == 0)
    {
        return false;
    }

    QByteArray erased(static_cast<int>(m_size), static_cast<char>(0xFF));
    m_data = erased;
    m_writtenBytes = 0;
    m_erasedRanges.clear();

    if (m_path.isEmpty())
    {
        return true;
    }

    m_file = new QFile(m_path);
    if (m_file->exists() && m_file->size() == static_cast<qint64>(m_size))
    {
        if (m_file->open(QIODevice::ReadWrite))
        {
            QByteArray fileData = m_file->readAll();
            if (fileData.size() == static_cast<int>(m_size))
            {
                m_data = fileData;
            }
            else
            {
                m_file->seek(0);
                m_file->write(m_data);
            }
            m_file->flush();
            return true;
        }
    }

    if (!m_file->open(QIODevice::ReadWrite | QIODevice::Truncate))
    {
        delete m_file;
        m_file = nullptr;
        return false;
    }

    m_file->write(m_data);
    m_file->flush();
    return true;
}

void FlashImage::close()
{
    if (m_file)
    {
        m_file->flush();
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
}

bool FlashImage::contains(quint32 address, quint32 length) const
{
    if (length == 0 || m_size == 0)
    {
        return false;
    }
    if (address < m_base)
    {
        return false;
    }
    quint64 offset = static_cast<quint64>(address) - m_base;
    return (offset + length) <= m_size;
}

bool FlashImage::eraseAll()
{
    if (m_data.isEmpty())
    {
        return false;
    }

    m_data.fill(static_cast<char>(0xFF));
    m_writtenBytes = 0;
    m_erasedRanges.clear();
    markErased(m_base, m_size);

    if (m_file)
    {
        m_file->seek(0);
        m_file->write(m_data);
        m_file->flush();
    }
    return true;
}

bool FlashImage::eraseRange(quint32 address, quint32 length)
{
    if (!contains(address, length))
    {
        return false;
    }

    quint32 offset = address - m_base;
    m_data.replace(static_cast<int>(offset), static_cast<int>(length), QByteArray(static_cast<int>(length), static_cast<char>(0xFF)));
    writeFile(address, QByteArray(static_cast<int>(length), static_cast<char>(0xFF)));
    markErased(address, length);
    return true;
}

bool FlashImage::write(quint32 address, const QByteArray &data)
{
    if (data.isEmpty() || !contains(address, static_cast<quint32>(data.size())))
    {
        return false;
    }

    quint32 offset = address - m_base;
    m_data.replace(static_cast<int>(offset), data.size(), data);
    m_writtenBytes += static_cast<quint32>(data.size());
    writeFile(address, data);
    return true;
}

bool FlashImage::isErased(quint32 address, quint32 length) const
{
    if (length == 0)
    {
        return true;
    }

    const quint64 end = static_cast<quint64>(address) + length;
    for (int i = 0; i < m_erasedRanges.size(); i++)
    {
        const quint64 rangeStart = m_erasedRanges.at(i).first;
        const quint64 rangeEnd = rangeStart + m_erasedRanges.at(i).second;
        if (address >= rangeStart && end <= rangeEnd)
        {
            return true;
        }
    }
    return false;
}

void FlashImage::markErased(quint32 address, quint32 length)
{
    if (length == 0)
    {
        return;
    }

    for (int i = 0; i < m_erasedRanges.size(); i++)
    {
        const quint64 rangeStart = m_erasedRanges.at(i).first;
        const quint64 rangeEnd = rangeStart + m_erasedRanges.at(i).second;
        if (address >= rangeStart && static_cast<quint64>(address) + length <= rangeEnd)
        {
            return;
        }
    }

    if (!m_erasedRanges.isEmpty())
    {
        QPair<quint32, quint32> &last = m_erasedRanges.last();
        if (static_cast<quint64>(last.first) + last.second == address)
        {
            last.second += length;
            return;
        }
        if (static_cast<quint64>(address) + length == last.first)
        {
            last.first = address;
            last.second += length;
            return;
        }
    }

    m_erasedRanges.append(qMakePair(address, length));
}

QByteArray FlashImage::read(quint32 address, quint32 length) const
{
    if (!contains(address, length))
    {
        return QByteArray();
    }

    quint32 offset = address - m_base;
    return m_data.mid(static_cast<int>(offset), static_cast<int>(length));
}

bool FlashImage::writeFile(quint32 address, const QByteArray &data)
{
    if (!m_file || !m_file->isOpen())
    {
        return false;
    }

    quint32 offset = address - m_base;
    m_file->seek(offset);
    qint64 written = m_file->write(data);
    return written == data.size();
}

bool FlashImage::save()
{
    if (m_path.isEmpty() || m_data.isEmpty())
    {
        return false;
    }

    if (m_file && m_file->isOpen())
    {
        return flush();
    }

    QFile file(m_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return false;
    }
    return file.write(m_data) == m_data.size();
}

bool FlashImage::flush()
{
    if (!m_file || !m_file->isOpen())
    {
        return false;
    }
    m_file->flush();
    return true;
}

QString FlashImage::getSha256() const
{
    QByteArray digest = QCryptographicHash::hash(m_data, QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toHex());
}
