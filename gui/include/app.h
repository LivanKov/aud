#ifndef APP_H
#define APP_H

#include <QMainWindow>

class QCustomPlot;

class App : public QMainWindow {
    Q_OBJECT
    
public:
    App(QWidget *parent = nullptr);
    ~App();
    
    void run();

private:
    QCustomPlot *timeDomainPlot;
    QCustomPlot *frequencyDomainPlot;
    
    void setupPlot(QCustomPlot *plot, const QString &xLabel, const QString &yLabel);
    void addSampleData();
};

#endif // APP_H
