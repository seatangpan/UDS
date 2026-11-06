#ifndef FLASHIMAGE_H
#define FLASHIMAGE_H

#include <QByteArray>
#include <QFile>
#include <QPair>
#include <QString>
#include <QVector>

// 模拟 ECU 的 Flash 镜像：按地址写入，数据同时落盘到镜像文件
class FlashImage
{
public:
    FlashImage();
    ~FlashImage();

    // 配置基址、大小与镜像文件路径，配置后需调用 open()
    void configure(quint32 base, quint32 size, const QString &path);

    // 打开镜像：文件存在且大小一致则读取，否则按 0xFF 新建
    bool open();
    void close();

    // 全片擦除（填充 0xFF）
    bool eraseAll();

    // 按地址范围擦除（填充 0xFF）
    bool eraseRange(quint32 address, quint32 length);

    // 按地址写入
    bool write(quint32 address, const QByteArray &data);

    // 地址范围是否处于已擦除状态
    bool isErased(quint32 address, quint32 length) const;

    // 按地址读取
    QByteArray read(quint32 address, quint32 length) const;

    bool contains(quint32 address, quint32 length) const;

    // 将内存镜像刷入文件
    bool save();
    bool flush();

    quint32 getBase() const { return m_base; }
    quint32 getSize() const { return m_size; }
    quint32 getWrittenBytes() const { return m_writtenBytes; }
    QString getPath() const { return m_path; }
    QString getSha256() const;

private:
    bool writeFile(quint32 address, const QByteArray &data);
    void markErased(quint32 address, quint32 length);

    QByteArray m_data;
    quint32 m_base;
    quint32 m_size;
    quint32 m_writtenBytes;
    QString m_path;
    QFile *m_file;
    QVector<QPair<quint32, quint32> > m_erasedRanges;
};

#endif // FLASHIMAGE_H
