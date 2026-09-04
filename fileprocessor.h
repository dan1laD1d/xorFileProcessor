#ifndef FILEPROCESSOR_H
#define FILEPROCESSOR_H

#include <QObject>
#include <QThread>
#include <QFile>
#include <QString>
#include <QAtomicInteger>
#include <QMutex>
#include <QWaitCondition>
#include <QTimer>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QHash>
#include <QByteArray>
#include <QRegularExpression>

class FileProcessor : public QObject
{
    Q_OBJECT

public:
    explicit FileProcessor(QObject *parent = nullptr);
    ~FileProcessor();

    struct Settings {
        QString fileMask = "*.txt";
        bool deleteInputFiles = false;
        QString outputPath;
        QString inputPath;
        bool overwriteExisting = true;
        bool useTimer = false;
        int pollIntervalMs = 1000;
        QByteArray xorValue;
    };

    void setSettings(const Settings &settings);
    void start();
    void stop();
    void pause();
    void resume();
    bool isPaused() const { return m_paused.load(); }
    bool isRunning() const { return m_running.load(); }

signals:
    void fileProcessingStarted(const QString &fileName);
    void fileProcessingProgress(const QString &fileName, qint64 processedBytes, qint64 totalBytes);
    void fileProcessingCompleted(const QString &fileName, bool success, const QString &error = QString());
    void fileProcessingPaused();
    void fileProcessingResumed();
    void allProcessingCompleted();
    void statusMessage(const QString &message);

private slots:
    void processFiles();
    void checkForFiles();

private:
    bool processFile(const QString &filePath);
    QString getOutputFilePath(const QString &inputFileName);
    void cleanUp();
    void waitForPause();

    Settings m_settings;
    QThread *m_thread;
    QTimer *m_timer;
    QAtomicInteger<bool> m_running;
    QAtomicInteger<bool> m_paused;
    QAtomicInteger<bool> m_stopRequested;
    QMutex m_pauseMutex;
    QWaitCondition m_pauseCondition;
    QHash<QString, qint64> m_pausePositions;
};

#endif // FILEPROCESSOR_H
