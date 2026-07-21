target("DefaultPollerTest1")
    set_kind("binary")
    add_files("main1.cpp")
    add_deps("muduo-core")

    add_runenvs(
        "MUDUO_DEFAULT_POLLER",
        "Poll"
    )
target_end()

target("DefaultPollerTest2")
    set_kind("binary")
    add_files("main2.cpp")
    add_deps("muduo-core")

    -- add_runenvs(
    --     "MUDUO_DEFAULT_POLLER",
    --     "Epoll"
    -- )
target_end()
