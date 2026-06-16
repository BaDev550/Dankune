file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h")
add_executable(Game ${GAME_SOURCES})

find_package(Vulkan REQUIRED COMPONENTS shaderc_combined)
find_package(VulkanMemoryAllocator REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(imgui REQUIRED)
find_package(glm REQUIRED) 
find_package(stb REQUIRED)

target_link_libraries(Game PRIVATE 
    glfw
    glm::glm-header-only
    imgui::imgui
    Vulkan::Vulkan
    Vulkan::shaderc_combined
    GPUOpen::VulkanMemoryAllocator
)

target_include_directories(Game PRIVATE 
    ${Stb_INCLUDE_DIR}
    $ENV{VULKAN_SDK}/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Engine
)