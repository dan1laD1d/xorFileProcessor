#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QScopedPointer>
#include "fileprocessor.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void on_startButton_clicked();
    void on_stopButton_clicked();
    void on_pauseResumeButton_clicked();
    void on_browseInputPath_clicked();
    void on_browseOutputPath_clicked();
    void onFileProcessingStarted(const QString &fileName);
    void onFileProcessingProgress(const QString &fileName, qint64 processedBytes, qint64 totalBytes);
    void onFileProcessingCompleted(const QString &fileName, bool success, const QString &error);
    void onStatusMessage(const QString &message);
    void onAllProcessingCompleted();

private:
    void updateUIState();
    void loadSettings();
    void saveSettings();
    bool validateSettings();

    Ui::MainWindow *ui;
    FileProcessor::Settings m_settings;
    QScopedPointer<FileProcessor> m_processor;
    bool m_processingActive = false;
};
#endif // MAINWINDOW_H
