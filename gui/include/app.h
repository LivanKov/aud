#ifndef APP_H
#define APP_H

#include <QMainWindow>
#include <QTimer>
#include <QVector>
#include <QMutex>
#include <atomic>

class QCustomPlot;
class QLabel;

class App : public QMainWindow {
    Q_OBJECT
    
public:
    App(QWidget *parent = nullptr);
    ~App();
    
    void run();
    
    // Called from audio thread to update data
    void updateAudioData(const double* timeData, const double* fftData, int size, double sampleRate);
    
    // Flag to signal audio thread to stop
    std::atomic<bool> shouldStop{false};

private slots:
    void refreshPlots();

private:
    QCustomPlot *timeDomainPlot;
    QCustomPlot *frequencyDomainPlot;
    QTimer *refreshTimer;
    QLabel *dominantFreqLabel;
    QLabel *closestNoteLabel;
    
    // Thread-safe data buffers
    QMutex dataMutex;
    QVector<double> timeBuffer;
    QVector<double> amplitudeBuffer;
    QVector<double> freqBuffer;
    QVector<double> magnitudeBuffer;
    bool dataReady{false};
    double currentSampleRate{44100.0};
    qint64 lastLabelUpdateTime{0};
    static constexpr qint64 LABEL_UPDATE_INTERVAL_MS = 250; // Update labels every 250ms
    
    void setupPlot(QCustomPlot *plot, const QString &xLabel, const QString &yLabel);
    void addSampleData();
    int findClosestNote(double frequency) const;

protected:
    void closeEvent(QCloseEvent *event) override;
};

#endif // APP_H
