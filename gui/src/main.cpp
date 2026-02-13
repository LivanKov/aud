#include <QApplication>
#include <iostream>
#include "app.h"

int main(int argc, char* argv[]) {
    std::cout << "Starting application..." << std::endl;
    
    QApplication qapp(argc, argv);
    
    App app;
    app.run();
    
    return qapp.exec();
}
