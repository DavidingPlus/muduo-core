includes("config-win32.lua", "config-linux.lua")


function is_current_win32()
    return is_plat("windows") or is_host("windows")
end

function is_current_linux()
    return is_plat("linux") or is_host("linux")
end

function define_current_platform_options()
    if is_current_win32() then
        define_win32_options()
        return
    end
    if is_current_linux() then
        define_linux_options()
        return
    end

    raise("Unknown platform")
end

function default_build_shared_for_current_platform()
    if is_current_win32() then
        return default_win32_build_shared()
    end
    if is_current_linux() then
        return default_linux_build_shared()
    end

    raise("Unknown platform")
end

function apply_current_platform_target_config()
    if is_current_win32() then
        apply_win32_target_config()
        return
    end
    if is_current_linux() then
        apply_linux_target_config()
        return
    end

    raise("Unknown platform")
end

function add_public_headers()
    local seen = {}

    for _, header in ipairs(os.files("src/**.h")) do
        local relative = path.relative(header, "src")
        local topmodule, _ = relative:match("^([^/\\]+)[/\\](.+)$")
        local filename = path.filename(header)
        local installkey = (topmodule and (topmodule .. "/") or "") .. filename

        if seen[installkey] then
            raise("duplicate public header install path: %s from %s and %s", installkey, seen[installkey], header)
        end
        seen[installkey] = header

        if topmodule then
            -- Example:
            --   src/foo/bar/baz.h -> include/foo/baz.h
            --   src/foo/bar.h     -> include/foo/bar.h
            add_headerfiles(header, {prefixdir = topmodule, filename = filename})
        else
            -- Example:
            --   src/foo.h -> include/foo.h
            add_headerfiles(header, {filename = filename})
        end
    end
end
