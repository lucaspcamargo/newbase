# Add audio system

newbase_add_system(NAME audio SOURCES 
    ./audio.cpp
    ./buffer.cpp
    ./converter.cpp
    ./producer/buffer.cpp
    ./producer/vorbis.cpp
    ./types.cpp
)
newbase_commit_systems()