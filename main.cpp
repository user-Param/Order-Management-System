#include "oms/oms.h"
#include <iostream>
#include <csignal>

std::unique_ptr<oms::OMS> g_oms;

void signalHandler(int) {
    if (g_oms) {
        std::cout << "\nShutting down OMS..." << std::endl;
        g_oms->stop();
    }
}

int main() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    g_oms = std::make_unique<oms::OMS>();
    g_oms->start();

    return 0;
}
