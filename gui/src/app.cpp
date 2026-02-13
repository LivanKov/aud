#include "app.h"
#include "qcustomplot.h"
#include <iostream>
#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>

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
    
    // Add sample data
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
    // Time domain - sine wave
    QVector<double> time(1000), amplitude(1000);
    for (int i = 0; i < 1000; ++i) {
        time[i] = i / 1000.0;
        amplitude[i] = qSin(2 * M_PI * 5 * time[i]) + 0.5 * qSin(2 * M_PI * 10 * time[i]);
    }
    timeDomainPlot->graph(0)->setData(time, amplitude);
    timeDomainPlot->xAxis->setRange(0, 1);
    timeDomainPlot->yAxis->setRange(-2, 2);
    timeDomainPlot->replot();
    
    // Frequency domain - sample spectrum
    QVector<double> freq(500), magnitude(500);
    for (int i = 0; i < 500; ++i) {
        freq[i] = i * 0.1;
        magnitude[i] = -60 + 60 * qExp(-qPow((i - 50) / 10.0, 2)) + 
                       40 * qExp(-qPow((i - 100) / 15.0, 2));
    }
    frequencyDomainPlot->graph(0)->setData(freq, magnitude);
    frequencyDomainPlot->xAxis->setRange(0, 50);
    frequencyDomainPlot->yAxis->setRange(-70, 0);
    frequencyDomainPlot->replot();
}

App::~App() {
    std::cout << "App destroyed" << std::endl;
}

void App::run() {
    // Show the window
    show();
}
