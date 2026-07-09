#include "simple_3dgs/vulkan_application.hpp"

#include <windows.h>

#include <cstdlib>
#include <exception>
#include <iostream>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    try {
        simple_3dgs::RunApplication(instance, showCommand);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "Simple 3DGS Engine", MB_OK | MB_ICONERROR);
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
