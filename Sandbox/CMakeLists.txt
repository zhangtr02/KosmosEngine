add_executable(Sandbox)

target_sources(Sandbox
    PRIVATE
        main.cpp
)

target_link_libraries(Sandbox
    PRIVATE
        KosmosEngine
)

