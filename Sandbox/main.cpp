#include "Core/Application.h"

#include <iostream>

int main()
{
    try
    {
        Kosmos::Application application;
        application.Run();
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "[Fatal Error] "
            << exception.what()
            << '\n';

        return 1;
    }
    
    return 0;
}