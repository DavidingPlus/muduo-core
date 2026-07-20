local function _gtest_summary_outdir()
    return path.join(os.projectdir(), "build", "gtest")
end

local function _gtest_summary_lockfile()
    return path.join(_gtest_summary_outdir(), "gtest-summary.lock")
end

local function _gtest_summary_printedfile(session)
    return path.join(_gtest_summary_outdir(), "gtest-summary." .. session .. ".printed")
end

local function _gtest_test_safe_name(name)
    return name:gsub("[/\\:]", "_")
end

local function _gtest_summary_resultfile(session, name)
    return path.join(_gtest_summary_outdir(), "gtest-summary." .. session .. "." .. _gtest_test_safe_name(name) .. ".xml")
end

local function _gtest_summary_donefile(session, name)
    return path.join(_gtest_summary_outdir(), "gtest-summary." .. session .. "." .. _gtest_test_safe_name(name) .. ".done")
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
    local cyan = "\27[96m"
    local green = "\27[32m"
    local red = "\27[31m"
    local yellow = "\27[33m"
    local bright = "\27[1m"
    local reset = "\27[0m"

    print("")
    print(string.format(
        "%sgtest summary:%s %s%d%s passed, %s%d%s failed, %s%d%s disabled, %s%d%s total",
        bright .. cyan, reset,
        green, totals.passed or 0, reset,
        red, (totals.failures or 0) + (totals.errors or 0), reset,
        yellow, totals.disabled or 0, reset,
        bright, totals.tests or 0, reset
    ))
    if (totals.skipped or 0) > 0 then
        print(string.format("%sgtest skipped:%s %s%d%s", bright .. cyan, reset, yellow, totals.skipped, reset))
    end
    if missing and #missing > 0 then
        print(string.format("%sgtest summary incomplete for:%s %s", bright .. yellow, reset, table.concat(missing, ", ")))
    end
end

function apply_gtest_summary_config(target_name, groups)
    local session = tostring(os.time()) .. "-" .. tostring(os.mclock())

    local function expected_test_names()
        local names = {}
        for _, group in ipairs(groups) do
            table.insert(names, target_name .. "/" .. group)
        end
        return names
    end

    local function ensure_outdir()
        return _gtest_summary_outdir()
    end

    before_test(function (target, opt)
        ensure_outdir()

        local lock = io.openlock(_gtest_summary_lockfile())
        assert(lock, "failed to open gtest summary lock")
        lock:lock()
        lock:unlock()
        lock:close()

        local resultfile = _gtest_summary_resultfile(session, opt.name)
        opt._gtest_resultfile = resultfile

        local runargs = table.wrap(opt.runargs or target:get("runargs"))
        table.insert(runargs, "--gtest_output=xml:" .. resultfile)
        opt.runargs = runargs
    end)

    after_test(function (_, opt)
        ensure_outdir()

        local lock = io.openlock(_gtest_summary_lockfile())
        assert(lock, "failed to open gtest summary lock")
        lock:lock()

        io.writefile(_gtest_summary_donefile(session, opt.name), "")

        local completed = 0
        for _, name in ipairs(expected_test_names()) do
            if os.isfile(_gtest_summary_donefile(session, name)) then
                completed = completed + 1
            end
        end

        local printedfile = _gtest_summary_printedfile(session)
        local should_print = completed == #groups and not os.isfile(printedfile)
        if should_print then
            io.writefile(printedfile, "")
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

            for _, name in ipairs(expected_test_names()) do
                local resultfile = _gtest_summary_resultfile(session, name)
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
end
