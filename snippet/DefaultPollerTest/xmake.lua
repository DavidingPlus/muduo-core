target("DefaultPollerTest")
    set_kind("binary")
    add_files("main.cpp")
    add_deps("muduo-core")

    add_runenvs(
        "MUDUO_DEFAULT_POLLER",
        "Poll"
    )
target_end()
