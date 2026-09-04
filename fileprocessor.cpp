#include "fileprocessor.h"
#include <QDebug>

FileProcessor::FileProcessor(QObject *parent)
    : QObject(parent)
    , m_thread(nullptr)
    , m_timer(nullptr)
    , m_running(false)
    , m_paused(false)
    , m_stopRequested(false)
{
    // Регистрируем тип для использования в сигналах/слотах
    qRegisterMetaType<qint64>("qint64");
}

FileProcessor::~FileProcessor()
{
    stop();
    cleanUp();
}

void FileProcessor::setSettings(const Settings &settings)
{
    m_settings = settings;

    if (m_settings.xorValue.size() != 8) {
        qWarning() << "XOR value must be 8 bytes";
    }
}

void FileProcessor::start()
{
    if (m_running.load()) {
        return;
    }

    m_running.store(true);
    m_stopRequested.store(false);
    m_paused.store(false);

    // Всегда используем отдельный поток для обработки
    if (!m_thread) {
        m_thread = new QThread(this);
        connect(m_thread, &QThread::finished, this, [this]() {
            m_running.store(false);
            emit allProcessingCompleted();
        });
    }

    // Перемещаем объект в рабочий поток
    this->moveToThread(m_thread);

    if (m_settings.useTimer) {
        // Для таймерного режима
        if (!m_timer) {
            m_timer = new QTimer();
            m_timer->setInterval(m_settings.pollIntervalMs);
            connect(m_timer, &QTimer::timeout, this, &FileProcessor::checkForFiles);
        }
        m_timer->moveToThread(m_thread);

        connect(m_thread, &QThread::started, this, [this]() {
            if (m_timer) {
                m_timer->start();
            }
            // Немедленная проверка
            checkForFiles();
        });
    } else {
        // Для разового режима
        connect(m_thread, &QThread::started, this, &FileProcessor::processFiles);
    }

    m_thread->start();
    emit statusMessage("Processing started...");
}

void FileProcessor::stop()
{
    if (!m_running.load()) {
        return;
    }

    m_stopRequested.store(true);

    // Будим поток, если он на паузе
    {
        QMutexLocker locker(&m_pauseMutex);
        m_pauseCondition.wakeAll();
    }

    if (m_timer && m_timer->isActive()) {
        m_timer->stop();
    }

    if (m_thread && m_thread->isRunning()) {
        m_thread->quit();
        m_thread->wait(5000);
        if (m_thread->isRunning()) {
            m_thread->terminate();
            m_thread->wait();
        }
    }

    m_running.store(false);
    m_paused.store(false);
    emit statusMessage("Processing stopped");
}

void FileProcessor::pause()
{
    if (!m_running.load() || m_paused.load()) {
        return;
    }

    qDebug() << "Pause requested from thread:" << QThread::currentThreadId();

    // Устанавливаем флаг паузы атомарно
    m_paused.store(true);

    emit statusMessage("Pause requested...");
    emit fileProcessingPaused();
}

void FileProcessor::resume()
{
    if (!m_running.load() || !m_paused.load()) {
        return;
    }

    qDebug() << "Resume requested from thread:" << QThread::currentThreadId();

    // Снимаем флаг паузы
    m_paused.store(false);

    // Будим рабочий поток
    {
        QMutexLocker locker(&m_pauseMutex);
        m_pauseCondition.wakeAll();
    }

    emit statusMessage("Resumed");
    emit fileProcessingResumed();
}

void FileProcessor::waitForPause()
{
    if (!m_paused.load()) {
        return;
    }

    qDebug() << "Worker thread entering pause state, thread:" << QThread::currentThreadId();
    emit statusMessage("Processing paused");

    QMutexLocker locker(&m_pauseMutex);
    while (m_paused.load() && !m_stopRequested.load()) {
        // Ждем сигнала о возобновлении или остановке
        m_pauseCondition.wait(&m_pauseMutex);
    }

    if (!m_stopRequested.load()) {
        qDebug() << "Worker thread resuming, thread:" << QThread::currentThreadId();
        emit statusMessage("Processing resumed");
    }
}

void FileProcessor::checkForFiles()
{
    if (!m_running.load() || m_stopRequested.load()) {
        return;
    }

    // Проверяем паузу перед началом
    waitForPause();

    if (m_stopRequested.load()) {
        return;
    }

    QDir dir(m_settings.inputPath);
    if (!dir.exists()) {
        emit statusMessage("Input directory does not exist: " + m_settings.inputPath);
        return;
    }

    QString mask = m_settings.fileMask.trimmed();

    if (mask.startsWith(".") && !mask.contains("*")) {
        mask = "*" + mask;
    }

    if (mask.isEmpty()) {
        mask = "*";
    }

    QStringList filters;
    filters << mask;
    dir.setNameFilters(filters);
    dir.setFilter(QDir::Files);

    QStringList files = dir.entryList();

    for (const QString &fileName : files) {
        if (m_stopRequested.load()) {
            break;
        }

        // Проверяем паузу перед каждым файлом
        waitForPause();

        if (m_stopRequested.load()) {
            break;
        }

        QString filePath = dir.absoluteFilePath(fileName);

        processFile(filePath);

        if (m_settings.deleteInputFiles && QFile::exists(filePath)) {
            QFile::remove(filePath);
        }
    }
}

void FileProcessor::processFiles()
{
    checkForFiles();
}

bool FileProcessor::processFile(const QString &filePath)
{
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        return false;
    }

    QString outputFilePath = getOutputFilePath(fileInfo.fileName());

    QFile inputFile(filePath);
    QFile outputFile(outputFilePath);

    if (!inputFile.open(QIODevice::ReadOnly)) {
        emit fileProcessingCompleted(fileInfo.fileName(), false, "Cannot open input file");
        return false;
    }

    QIODevice::OpenMode outputMode = QIODevice::WriteOnly;
    if (m_settings.overwriteExisting) {
        outputMode |= QIODevice::Truncate;
    }

    if (!outputFile.open(outputMode)) {
        inputFile.close();
        emit fileProcessingCompleted(fileInfo.fileName(), false, "Cannot open output file");
        return false;
    }

    emit fileProcessingStarted(fileInfo.fileName());
    emit statusMessage("Processing: " + fileInfo.fileName());

    qint64 totalBytes = inputFile.size();
    qint64 processedBytes = 0;
    qint64 startPosition = 0;

    if (m_pausePositions.contains(filePath)) {
        startPosition = m_pausePositions[filePath];
        inputFile.seek(startPosition);
        outputFile.seek(startPosition);
        processedBytes = startPosition;
        m_pausePositions.remove(filePath);
    }

    const qint64 bufferSize = 1024 * 1024; // 1MB buffer
    QByteArray buffer;
    buffer.resize(static_cast<int>(bufferSize));

    bool success = true;
    QString errorMessage;

    while (processedBytes < totalBytes && !m_stopRequested.load()) {
        // Проверяем паузу в начале каждой итерации
        waitForPause();

        if (m_stopRequested.load()) {
            success = false;
            errorMessage = "Processing stopped";
            break;
        }

        qint64 bytesToRead = qMin(bufferSize, totalBytes - processedBytes);
        qint64 bytesRead = inputFile.read(buffer.data(), bytesToRead);

        if (bytesRead <= 0) {
            break;
        }

        // XOR операция
        char *data = buffer.data();
        const char *xorData = m_settings.xorValue.constData();
        for (qint64 i = 0; i < bytesRead; ++i) {
            int xorIndex = static_cast<int>(i % 8);
            data[i] = data[i] ^ xorData[xorIndex];
        }

        qint64 bytesWritten = outputFile.write(buffer.data(), bytesRead);
        if (bytesWritten != bytesRead) {
            success = false;
            errorMessage = "Write error";
            break;
        }

        processedBytes += bytesRead;
        emit fileProcessingProgress(fileInfo.fileName(), processedBytes, totalBytes);

        // Даем возможность обработки событий
        QThread::msleep(10);
    }

    inputFile.close();
    outputFile.close();

    if (success && !m_stopRequested.load()) {
        m_pausePositions.remove(filePath);
        emit fileProcessingCompleted(fileInfo.fileName(), true);
        emit statusMessage("Completed: " + fileInfo.fileName());
        return true;
    } else {
        if (m_stopRequested.load()) {
            emit fileProcessingCompleted(fileInfo.fileName(), false, "Stopped by user");
        } else {
            emit fileProcessingCompleted(fileInfo.fileName(), false, errorMessage);
        }
        return false;
    }
}

QString FileProcessor::getOutputFilePath(const QString &inputFileName)
{
    QDir outputDir(m_settings.outputPath);
    if (!outputDir.exists()) {
        outputDir.mkpath(".");
    }

    QString outputFilePath = outputDir.absoluteFilePath(inputFileName);

    if (!m_settings.overwriteExisting && QFile::exists(outputFilePath)) {
        QFileInfo fileInfo(outputFilePath);
        QString baseName = fileInfo.completeBaseName();
        QString extension = fileInfo.suffix();
        int counter = 1;

        while (QFile::exists(outputFilePath)) {
            QString newFileName = baseName + "_" + QString::number(counter);
            if (!extension.isEmpty()) {
                newFileName += "." + extension;
            }
            outputFilePath = outputDir.absoluteFilePath(newFileName);
            counter++;
        }
    }

    return outputFilePath;
}

void FileProcessor::cleanUp()
{
    if (m_timer) {
        m_timer->stop();
        delete m_timer;
        m_timer = nullptr;
    }

    if (m_thread) {
        if (m_thread->isRunning()) {
            m_thread->quit();
            m_thread->wait();
        }
        delete m_thread;
        m_thread = nullptr;
    }
}

#include "fileprocessor.moc"
