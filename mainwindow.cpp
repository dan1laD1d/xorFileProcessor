#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QSettings>
#include <QStandardPaths>
#include <QDateTime>
#include <QRegularExpression>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_processingActive(false)
{
    ui->setupUi(this);

    // Устанавливаем значения по умолчанию
    ui->fileMaskEdit->setText("*.txt");
    ui->fileMaskEdit->setPlaceholderText("Например: *.txt, testFile.bin, data*.*");
    ui->fileMaskEdit->setToolTip("Маска файлов для обработки.\n"
                                 "Примеры:\n"
                                 "*.txt - все текстовые файлы\n"
                                 "testFile.bin - конкретный файл\n"
                                 "*.bin - все бинарные файлы\n"
                                 "data* - файлы, начинающиеся с 'data'");

    ui->xorValueEdit->setText("1234567890ABCDEF");
    ui->pollIntervalSpinBox->setValue(1000);
    ui->deleteInputCheckBox->setChecked(false);
    ui->overwriteCheckBox->setChecked(true);
    ui->useTimerCheckBox->setChecked(false);
    ui->inputPathEdit->setText(QDir::currentPath());
    ui->outputPathEdit->setText(QDir::currentPath() + "/output");

    // Настраиваем таблицу прогресса
    ui->progressTable->setColumnCount(4);
    ui->progressTable->setHorizontalHeaderLabels({"Файл", "Прогресс", "Статус", "Размер"});
    ui->progressTable->horizontalHeader()->setStretchLastSection(true);
    ui->progressTable->setColumnWidth(0, 200);
    ui->progressTable->setColumnWidth(1, 100);
    ui->progressTable->setColumnWidth(2, 150);

    // Подключаем сигналы
    connect(ui->startButton, &QPushButton::clicked, this, &MainWindow::on_startButton_clicked);
    connect(ui->stopButton, &QPushButton::clicked, this, &MainWindow::on_stopButton_clicked);
    connect(ui->pauseResumeButton, &QPushButton::clicked, this, &MainWindow::on_pauseResumeButton_clicked);
    connect(ui->browseInputButton, &QPushButton::clicked, this, &MainWindow::on_browseInputPath_clicked);
    connect(ui->browseOutputButton, &QPushButton::clicked, this, &MainWindow::on_browseOutputPath_clicked);

    loadSettings();
    updateUIState();

    ui->statusLabel->setText("Готов к работе");
}

MainWindow::~MainWindow()
{
    saveSettings();
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_processor && m_processor->isRunning()) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, "Подтверждение",
            "Обработка файлов все еще выполняется. Вы уверены, что хотите выйти?",
            QMessageBox::Yes | QMessageBox::No
            );

        if (reply == QMessageBox::No) {
            event->ignore();
            return;
        }

        // Останавливаем обработку
        m_processor->stop();
    }
    event->accept();
}

bool MainWindow::validateSettings()
{
    QString mask = ui->fileMaskEdit->text().trimmed();

    if (mask.isEmpty()) {
        QMessageBox::warning(this, "Ошибка", "Введите маску файлов");
        return false;
    }

    // Проверяем hex значение
    QString hexValue = ui->xorValueEdit->text();
    hexValue.remove("0x", Qt::CaseInsensitive);
    hexValue.remove(QRegularExpression("[^0-9A-Fa-f]"));

    if (hexValue.length() != 16) {
        QMessageBox::warning(this, "Ошибка", "Введите корректное 8-байтное hex значение");
        return false;
    }

    // Проверяем пути
    QDir inputDir(ui->inputPathEdit->text());
    if (!inputDir.exists()) {
        QMessageBox::warning(this, "Ошибка", "Входная директория не существует");
        return false;
    }

    return true;
}

void MainWindow::on_startButton_clicked()
{
    if (m_processingActive) {
        return;
    }

    if (!validateSettings()) {
        return;
    }

    // Собираем настройки
    m_settings.fileMask = ui->fileMaskEdit->text().trimmed();
    m_settings.deleteInputFiles = ui->deleteInputCheckBox->isChecked();
    m_settings.outputPath = ui->outputPathEdit->text();
    m_settings.inputPath = ui->inputPathEdit->text();
    m_settings.overwriteExisting = ui->overwriteCheckBox->isChecked();
    m_settings.useTimer = ui->useTimerCheckBox->isChecked();
    m_settings.pollIntervalMs = ui->pollIntervalSpinBox->value();

    // Обработка hex значения
    QString hexValue = ui->xorValueEdit->text();
    hexValue.remove("0x", Qt::CaseInsensitive);
    hexValue.remove(QRegularExpression("[^0-9A-Fa-f]"));
    m_settings.xorValue = QByteArray::fromHex(hexValue.toLatin1());

    // Создаем процессор
    m_processor.reset(new FileProcessor());

    // Подключаем сигналы
    connect(m_processor.data(), &FileProcessor::fileProcessingStarted,
            this, &MainWindow::onFileProcessingStarted);
    connect(m_processor.data(), &FileProcessor::fileProcessingProgress,
            this, &MainWindow::onFileProcessingProgress);
    connect(m_processor.data(), &FileProcessor::fileProcessingCompleted,
            this, &MainWindow::onFileProcessingCompleted);
    connect(m_processor.data(), &FileProcessor::statusMessage,
            this, &MainWindow::onStatusMessage);
    connect(m_processor.data(), &FileProcessor::allProcessingCompleted,
            this, &MainWindow::onAllProcessingCompleted);

    m_processor->setSettings(m_settings);
    m_processor->start();

    m_processingActive = true;
    updateUIState();
}

void MainWindow::on_stopButton_clicked()
{
    if (m_processor && m_processor->isRunning()) {
        m_processor->stop();
        m_processingActive = false;
        updateUIState();
        ui->statusLabel->setText("Обработка остановлена");
        ui->pauseResumeButton->setText("Пауза");
    }
}

void MainWindow::on_pauseResumeButton_clicked()
{
    if (m_processor && m_processor->isRunning()) {
        if (m_processor->isPaused()) {
            m_processor->resume();
            ui->pauseResumeButton->setText("Пауза");
        } else {
            m_processor->pause();
            ui->pauseResumeButton->setText("Продолжить");
        }
    }
}

void MainWindow::on_browseInputPath_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Выберите входную директорию",
                                                    ui->inputPathEdit->text());
    if (!dir.isEmpty()) {
        ui->inputPathEdit->setText(dir);
    }
}

void MainWindow::on_browseOutputPath_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Выберите выходную директорию",
                                                    ui->outputPathEdit->text());
    if (!dir.isEmpty()) {
        ui->outputPathEdit->setText(dir);
    }
}

void MainWindow::onFileProcessingStarted(const QString &fileName)
{
    int row = ui->progressTable->rowCount();
    ui->progressTable->insertRow(row);
    ui->progressTable->setItem(row, 0, new QTableWidgetItem(fileName));
    ui->progressTable->setItem(row, 1, new QTableWidgetItem("0%"));
    ui->progressTable->setItem(row, 2, new QTableWidgetItem("В обработке..."));
    ui->progressTable->setItem(row, 3, new QTableWidgetItem(""));
}

void MainWindow::onFileProcessingProgress(const QString &fileName, qint64 processedBytes, qint64 totalBytes)
{
    // Находим строку с этим файлом
    for (int row = 0; row < ui->progressTable->rowCount(); ++row) {
        QTableWidgetItem *item = ui->progressTable->item(row, 0);
        if (item && item->text() == fileName) {
            int percent = (totalBytes > 0) ? (processedBytes * 100 / totalBytes) : 0;
            ui->progressTable->item(row, 1)->setText(QString::number(percent) + "%");

            // Форматируем размер
            QString sizeStr;
            if (totalBytes >= 1024 * 1024 * 1024) {
                sizeStr = QString::number(processedBytes / (1024.0 * 1024 * 1024), 'f', 2) + " GB / " +
                          QString::number(totalBytes / (1024.0 * 1024 * 1024), 'f', 2) + " GB";
            } else if (totalBytes >= 1024 * 1024) {
                sizeStr = QString::number(processedBytes / (1024.0 * 1024), 'f', 2) + " MB / " +
                          QString::number(totalBytes / (1024.0 * 1024), 'f', 2) + " MB";
            } else if (totalBytes >= 1024) {
                sizeStr = QString::number(processedBytes / 1024.0, 'f', 2) + " KB / " +
                          QString::number(totalBytes / 1024.0, 'f', 2) + " KB";
            } else {
                sizeStr = QString::number(processedBytes) + " / " + QString::number(totalBytes) + " B";
            }

            ui->progressTable->item(row, 3)->setText(sizeStr);
            break;
        }
    }
}

void MainWindow::onFileProcessingCompleted(const QString &fileName, bool success, const QString &error)
{
    for (int row = 0; row < ui->progressTable->rowCount(); ++row) {
        QTableWidgetItem *item = ui->progressTable->item(row, 0);
        if (item && item->text() == fileName) {
            if (success) {
                ui->progressTable->item(row, 2)->setText("Завершено");
                ui->progressTable->item(row, 1)->setText("100%");
            } else {
                ui->progressTable->item(row, 2)->setText("Ошибка: " + error);
            }
            break;
        }
    }
}

void MainWindow::onStatusMessage(const QString &message)
{
    ui->statusLabel->setText(message);
    ui->logTextEdit->append(QDateTime::currentDateTime().toString("HH:mm:ss") + " - " + message);
}

void MainWindow::onAllProcessingCompleted()
{
    m_processingActive = false;
    updateUIState();
    ui->statusLabel->setText("Обработка завершена");
    ui->pauseResumeButton->setText("Пауза");
}

void MainWindow::updateUIState()
{
    ui->startButton->setEnabled(!m_processingActive);
    ui->stopButton->setEnabled(m_processingActive);
    ui->pauseResumeButton->setEnabled(m_processingActive);

    // Блокируем настройки во время обработки
    ui->fileMaskEdit->setEnabled(!m_processingActive);
    ui->xorValueEdit->setEnabled(!m_processingActive);
    ui->pollIntervalSpinBox->setEnabled(!m_processingActive);
    ui->deleteInputCheckBox->setEnabled(!m_processingActive);
    ui->overwriteCheckBox->setEnabled(!m_processingActive);
    ui->useTimerCheckBox->setEnabled(!m_processingActive);
    ui->inputPathEdit->setEnabled(!m_processingActive);
    ui->outputPathEdit->setEnabled(!m_processingActive);
    ui->browseInputButton->setEnabled(!m_processingActive);
    ui->browseOutputButton->setEnabled(!m_processingActive);
}

void MainWindow::loadSettings()
{
    QSettings settings("FileXORProcessor", "Settings");
    ui->fileMaskEdit->setText(settings.value("fileMask", "*.txt").toString());
    ui->xorValueEdit->setText(settings.value("xorValue", "1234567890ABCDEF").toString());
    ui->pollIntervalSpinBox->setValue(settings.value("pollInterval", 1000).toInt());
    ui->deleteInputCheckBox->setChecked(settings.value("deleteInput", false).toBool());
    ui->overwriteCheckBox->setChecked(settings.value("overwrite", true).toBool());
    ui->useTimerCheckBox->setChecked(settings.value("useTimer", false).toBool());
    ui->inputPathEdit->setText(settings.value("inputPath", QDir::currentPath()).toString());
    ui->outputPathEdit->setText(settings.value("outputPath", QDir::currentPath() + "/output").toString());
}

void MainWindow::saveSettings()
{
    QSettings settings("FileXORProcessor", "Settings");
    settings.setValue("fileMask", ui->fileMaskEdit->text());
    settings.setValue("xorValue", ui->xorValueEdit->text());
    settings.setValue("pollInterval", ui->pollIntervalSpinBox->value());
    settings.setValue("deleteInput", ui->deleteInputCheckBox->isChecked());
    settings.setValue("overwrite", ui->overwriteCheckBox->isChecked());
    settings.setValue("useTimer", ui->useTimerCheckBox->isChecked());
    settings.setValue("inputPath", ui->inputPathEdit->text());
    settings.setValue("outputPath", ui->outputPathEdit->text());
}

#include "mainwindow.moc"
