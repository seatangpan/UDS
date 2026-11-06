#include "securityaccess.h"
#include <QRandomGenerator>

#define SECURITY_MAX_ATTEMPTS 3

SecurityAccess::SecurityAccess()
{
    reset();
}

void SecurityAccess::reset()
{
    m_lastSeed.clear();
    m_pendingLevel = 0;
    m_unlockedLevel = 0;
    m_failedAttempts = 0;
    m_attemptsExceeded = false;
}

QByteArray SecurityAccess::generateSeed(quint8 level)
{
    if (level != 0x01 && level != 0x03)
    {
        return QByteArray();
    }

    m_pendingLevel = level;
    m_lastSeed.clear();

    quint32 value = QRandomGenerator::global()->generate();
    m_lastSeed.append(static_cast<char>((value >> 24) & 0xFF));
    m_lastSeed.append(static_cast<char>((value >> 16) & 0xFF));
    m_lastSeed.append(static_cast<char>((value >> 8) & 0xFF));
    m_lastSeed.append(static_cast<char>(value & 0xFF));
    return m_lastSeed;
}

bool SecurityAccess::hasSeed(quint8 level) const
{
    return m_pendingLevel == level && m_lastSeed.size() == 4;
}

bool SecurityAccess::verifyKey(quint8 level, const QByteArray &key)
{
    if (!hasSeed(level))
    {
        return false;
    }

    if (m_attemptsExceeded)
    {
        return false;
    }

    // TODO: 在此加入 seed -> key 校验算法，当前仅校验长度
    bool valid = (key.size() == 4);

    if (valid)
    {
        m_unlockedLevel = level;
        m_failedAttempts = 0;
        m_lastSeed.clear();
    }
    else
    {
        m_failedAttempts++;
        if (m_failedAttempts >= SECURITY_MAX_ATTEMPTS)
        {
            m_attemptsExceeded = true;
        }
    }

    return valid;
}
