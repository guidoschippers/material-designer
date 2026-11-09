#include "md/core/Version.h"

#include <iostream>

int main()
{
    std::cout << "material-designer-cli " << md::version() << "\n";
    return 0;
}
