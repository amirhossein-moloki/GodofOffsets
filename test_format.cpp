#include <format>
#include <iostream>
#include <string>

int main() {
    std::cout << std::format("Hello, {}!", "world") << std::endl;
    return 0;
}
