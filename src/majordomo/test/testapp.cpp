#include <iostream>
#include <random>

#include <majordomo/broker.hpp>

int main(int /*argc*/, char ** /*argv*/) {
    yaz::Context               context;
    Majordomo::OpenCMW::Broker broker("", "", context);
    broker.run();
}
