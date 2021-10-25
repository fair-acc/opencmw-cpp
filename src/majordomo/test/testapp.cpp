#include <iostream>
#include <random>

#include <majordomo/broker.hpp>

int main(int /*argc*/, char ** /*argv*/) {
    Majordomo::OpenCMW::Broker broker("", "");
    broker.run();
}

