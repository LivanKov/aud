#include "app.h"
#include "qcustomplot.h"
#include <iostream>
#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QCloseEvent>
#include <cmath>

App::App(QWidget *parent) : QMainWindow(parent) {
    std::cout << "App initialized" << std::endl;
    
    // Set window properties
    setWindowTitle("Audio Capture - Time & Frequency Domain");
    resize(900, 700);
    setMinimumSize(600, 500);
    
    // Set black background
    setStyleSheet("QMainWindow { background-color: #000000; }");
    
    // Create central widget
    QWidget *centralWidget = new QWidget(this);
    centralWidget->setStyleSheet("background-color: #000000;");
    setCentralWidget(centralWidget);
    
    // Create vertical layout
    QVBoxLayout *layout = new QVBoxLayout(centralWidget);
    layout->setSpacing(15);
    layout->setContentsMargins(15, 15, 15, 15);
    
    // Time Domain Plot
    QLabel *timeLabel = new QLabel("Time Domain", centralWidget);
    timeLabel->setStyleSheet("color: #CCCCCC; font-size: 14px; font-weight: bold;");
    layout->addWidget(timeLabel);
    
    timeDomainPlot = new QCustomPlot(centralWidget);
    setupPlot(timeDomainPlot, "Time (s)", "Amplitude");
    layout->addWidget(timeDomainPlot, 1);
    
    // Frequency Domain Plot
    QLabel *freqLabel = new QLabel("Frequency Domain", centralWidget);
    freqLabel->setStyleSheet("color: #CCCCCC; font-size: 14px; font-weight: bold;");
    layout->addWidget(freqLabel);
    
    frequencyDomainPlot = new QCustomPlot(centralWidget);
    setupPlot(frequencyDomainPlot, "Frequency (Hz)", "Magnitude (dB)");
    layout->addWidget(frequencyDomainPlot, 1);
    
    // Setup refresh timer for real-time updates
    refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &App::refreshPlots);
    refreshTimer->start(33); // ~30 FPS
    
    // Add initial sample data
    addSampleData();
}

void App::setupPlot(QCustomPlot *plot, const QString &xLabel, const QString &yLabel) {
    // Dark theme colors
    QColor bgColor(26, 26, 26);
    QColor gridColor(60, 60, 60);
    QColor axisColor(150, 150, 150);
    QColor plotLineColor(100, 200, 255);
    
    // Set background
    plot->setBackground(bgColor);
    
    // Configure axes
    plot->xAxis->setLabel(xLabel);
    plot->yAxis->setLabel(yLabel);
    plot->xAxis->setLabelColor(axisColor);
    plot->yAxis->setLabelColor(axisColor);
    plot->xAxis->setTickLabelColor(axisColor);
    plot->yAxis->setTickLabelColor(axisColor);
    plot->xAxis->setBasePen(QPen(axisColor));
    plot->yAxis->setBasePen(QPen(axisColor));
    plot->xAxis->setTickPen(QPen(axisColor));
    plot->yAxis->setTickPen(QPen(axisColor));
    plot->xAxis->setSubTickPen(QPen(axisColor));
    plot->yAxis->setSubTickPen(QPen(axisColor));
    
    // Enable grid
    plot->xAxis->grid()->setVisible(true);
    plot->yAxis->grid()->setVisible(true);
    plot->xAxis->grid()->setPen(QPen(gridColor, 1, Qt::SolidLine));
    plot->yAxis->grid()->setPen(QPen(gridColor, 1, Qt::SolidLine));
    plot->xAxis->grid()->setSubGridVisible(true);
    plot->yAxis->grid()->setSubGridVisible(true);
    plot->xAxis->grid()->setSubGridPen(QPen(gridColor, 1, Qt::DotLine));
    plot->yAxis->grid()->setSubGridPen(QPen(gridColor, 1, Qt::DotLine));
    
    // Add a graph
    plot->addGraph();
    plot->graph(0)->setPen(QPen(plotLineColor, 2));
    
    // Enable interactions
    plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
}

void App::addSampleData() {
    // Time domain - flat line until real data arrives
    QVector<double> time(512), amplitude(512);
    for (int i = 0; i < 512; ++i) {
        time[i] = i / 44100.0;
        amplitude[i] = 0;
    }
    timeDomainPlot->graph(0)->setData(time, amplitude);
    timeDomainPlot->xAxis->setRange(0, 512.0 / 44100.0);
    timeDomainPlot->yAxis->setRange(-1, 1);
    timeDomainPlot->replot();
    
    // Frequency domain - flat line until real data arrives
    QVector<double> freq(256), magnitude(256);
    for (int i = 0; i < 256; ++i) {
        freq[i] = i * 44100.0 / 512.0;
        magnitude[i] = -80;
    }
    frequencyDomainPlot->graph(0)->setData(freq, magnitude);
    frequencyDomainPlot->xAxis->setRange(20, 20000);
    frequencyDomainPlot->xAxis->setScaleType(QCPAxis::stLogarithmic);
    QSharedPointer<QCPAxisTickerLog> logTicker(new QCPAxisTickerLog);
    frequencyDomainPlot->xAxis->setTicker(logTicker);
    frequencyDomainPlot->yAxis->setRange(-80, 0);
    frequencyDomainPlot->replot();
}

void App::updateAudioData(const double* timeData, const double* fftData, int size, double sampleRate) {
    QMutexLocker locker(&dataMutex);
    
    currentSampleRate = sampleRate;
    
    // Copy time domain data
    timeBuffer.resize(size);
    amplitudeBuffer.resize(size);
    for (int i = 0; i < size; ++i) {
        timeBuffer[i] = i / sampleRate;
        amplitudeBuffer[i] = timeData[i];
    }
    
    // Copy and convert FFT data to magnitude in dB
    int fftSize = size / 2;
    freqBuffer.resize(fftSize);
    magnitudeBuffer.resize(fftSize);
    
    for (int i = 0; i < fftSize; ++i) {
        freqBuffer[i] = i * sampleRate / size;
        // Convert to dB with floor at -80 dB
        double mag = std::abs(fftData[i]) / size;
        if (mag < 1e-10) mag = 1e-10;
        magnitudeBuffer[i] = 20.0 * std::log10(mag);
        if (magnitudeBuffer[i] < -80) magnitudeBuffer[i] = -80;
    }
    
    dataReady = true;
}

void App::refreshPlots() {
    QMutexLocker locker(&dataMutex);
    
    if (!dataReady) return;
    
    // Update time domain plot
    timeDomainPlot->graph(0)->setData(timeBuffer, amplitudeBuffer);
    timeDomainPlot->xAxis->setRange(0, timeBuffer.size() / currentSampleRate);
    
    // Auto-scale Y axis based on data
    double maxAmp = 0.01;
    for (const double& a : amplitudeBuffer) {
        if (std::abs(a) > maxAmp) maxAmp = std::abs(a);
    }
    timeDomainPlot->yAxis->setRange(-maxAmp * 1.1, maxAmp * 1.1);
    timeDomainPlot->replot();
    
    // Update frequency domain plot
    frequencyDomainPlot->graph(0)->setData(freqBuffer, magnitudeBuffer);
    frequencyDomainPlot->replot();
    
    dataReady = false;
}

void App::closeEvent(QCloseEvent *event) {
    std::cout << "Window closing, signaling audio thread to stop..." << std::endl;
    shouldStop = true;
    event->accept();
}

App::~App() {
    refreshTimer->stop();
    std::cout << "App destroyed" << std::endl;
}

void App::run() {
    // Show the window
    show();
}
