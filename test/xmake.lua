if not get_config("with_gtest") then
    return
end

add_requires("gtest")

local gtest_groups = {
    "core",
    "utils",
    "testrun"
}

local gtest_session = tostring(os.time()) .. "-" .. tostring(os.mclock())

local function _gtest_summary_root()
    return path.join(os.projectdir(), ".xmake")
end

local function _gtest_summary_lockfile()
    return path.join(_gtest_summary_root(), "gtest-summary.lock")
end

local function _gtest_summary_printedfile()
    return path.join(_gtest_summary_root(), "gtest-summary." .. gtest_session .. ".printed")
end

local function _gtest_expected_test_names()
    local names = {}
    for _, group in ipairs(gtest_groups) do
        table.insert(names, "tests/" .. group)
    end
    return names
end

local function _gtest_test_safe_name(opt)
    return opt.name:gsub("[/\\:]", "_")
end

local function _gtest_summary_resultfile(opt)
    return path.join(_gtest_summary_root(), "gtest-summary." .. gtest_session .. "." .. _gtest_test_safe_name(opt) .. ".xml")
end

local function _gtest_summary_donefile(opt)
    return path.join(_gtest_summary_root(), "gtest-summary." .. gtest_session .. "." .. _gtest_test_safe_name(opt) .. ".done")
end

local function _gtest_parse_result_xml(xml)
    xml = xml or ""
    local tests = tonumber(xml:match('<testsuites.- tests="(%d+)"')) or 0
    local failures = tonumber(xml:match('<testsuites.- failures="(%d+)"')) or 0
    local disabled = tonumber(xml:match('<testsuites.- disabled="(%d+)"')) or 0
    local skipped = tonumber(xml:match('<testsuites.- skipped="(%d+)"')) or 0
    local errors = tonumber(xml:match('<testsuites.- errors="(%d+)"')) or 0
    return {
        tests = tests,
        failures = failures,
        disabled = disabled,
        skipped = skipped,
        errors = errors,
        passed = math.max(0, tests - failures - disabled - skipped - errors)
    }
end

local function _gtest_print_summary(totals, missing)
    local summary = string.format(
        "gtest summary: %d passed, %d failed, %d disabled",
        totals.passed or 0,
        (totals.failures or 0) + (totals.errors or 0),
        totals.disabled or 0
    )
    if (totals.skipped or 0) > 0 then
        summary = summary .. string.format(", %d skipped", totals.skipped)
    end
    summary = summary .. string.format(", out of %d test(s)", totals.tests or 0)
    print("")
    print(summary)
    if missing and #missing > 0 then
        print("gtest summary incomplete for: " .. table.concat(missing, ", "))
    end
end

-- 请用 xmake test 执行测试。
target("tests")
    set_default(false)

    set_kind("binary")
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/test")
    add_deps("muduo-core")
    add_packages("gtest")

    on_install(function () end)
    apply_current_platform_target_config()

    add_files("main.cpp")

    before_test(function (target, opt)
        local lock = io.openlock(_gtest_summary_lockfile())
        assert(lock, "failed to open gtest summary lock")
        lock:lock()
        lock:unlock()
        lock:close()

        local resultfile = _gtest_summary_resultfile(opt)
        opt._gtest_resultfile = resultfile

        local runargs = table.wrap(opt.runargs or target:get("runargs"))
        table.insert(runargs, "--gtest_output=xml:" .. resultfile)
        opt.runargs = runargs
    end)

    after_test(function (_, opt)
        local lock = io.openlock(_gtest_summary_lockfile())
        assert(lock, "failed to open gtest summary lock")
        lock:lock()

        local donefile = _gtest_summary_donefile(opt)
        io.writefile(donefile, "")

        local completed = 0
        for _, name in ipairs(_gtest_expected_test_names()) do
            if os.isfile(_gtest_summary_donefile({name = name})) then
                completed = completed + 1
            end
        end

        local should_print = completed == #gtest_groups and not os.isfile(_gtest_summary_printedfile())
        if should_print then
            io.writefile(_gtest_summary_printedfile(), "")
        end
        lock:unlock()
        lock:close()

        if should_print then
            local totals = {
                tests = 0,
                passed = 0,
                failures = 0,
                disabled = 0,
                skipped = 0,
                errors = 0
            }
            local missing = {}
            for _, name in ipairs(_gtest_expected_test_names()) do
                local resultfile = _gtest_summary_resultfile({name = name})
                if os.isfile(resultfile) then
                    local report = _gtest_parse_result_xml(io.readfile(resultfile))
                    for key, value in pairs(report) do
                        totals[key] = (totals[key] or 0) + value
                    end
                else
                    table.insert(missing, name)
                end
            end
            _gtest_print_summary(totals, missing)
        end
    end)

    for _, group in ipairs(gtest_groups) do
        add_tests(group, {
            realtime_output = true,
            files = {group .. "/*.cpp"}
        })
    end
target_end()
