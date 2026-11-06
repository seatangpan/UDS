#ifndef SECURITYACCESS_H
#define SECURITYACCESS_H

#include <QByteArray>

// 安全访问 0x27 的种子/钥匙管理
class SecurityAccess
{
public:
    SecurityAccess();

    // 会话切换或复位时调用，清空解锁状态与尝试次数
    void reset();

    // 生成种子，返回空表示安全等级不支持
    QByteArray generateSeed(quint8 level);

    bool hasSeed(quint8 level) const;
    bool verifyKey(quint8 level, const QByteArray &key);

    int getUnlockedLevel() const { return m_unlockedLevel; }
    bool isAttemptsExceeded() const { return m_attemptsExceeded; }

private:
    QByteArray m_lastSeed;
    quint8 m_pendingLevel;
    int m_unlockedLevel;
    int m_failedAttempts;
    bool m_attemptsExceeded;
};

#endif // SECURITYACCESS_H
