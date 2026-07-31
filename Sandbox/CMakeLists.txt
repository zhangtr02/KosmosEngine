add_executable(Sandbox)

target_sources(Sandbox
    PRIVATE
        main.cpp
        Scenes/DemoScene.cpp
        Scenes/DemoScene.h
        Editor/EditorLayer.cpp
        Editor/EditorLayer.h
)

target_include_directories(Sandbox
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(Sandbox
    PRIVATE
        KosmosEngine
        imgui
)

target_compile_definitions(Sandbox
    PRIVATE
        KOSMOS_ASSET_DIR="${PROJECT_SOURCE_DIR}/Assets"
)

