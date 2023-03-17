add_rules("mode.debug", "mode.release")

add_requires("libcurl", "libxml2")

set_languages("c99", "c++11")

target("main")
    set_kind("binary")
    add_files("main.cpp")
    add_files("bloom.cpp")
    add_files("spider.cpp")
add_packages("libcurl", "libxml2")

