local public_header_manifest = nil

local function normalize_path(filepath)
    return path.absolute(filepath):gsub("\\", "/")
end

local function get_public_header_prefix(target)
    return target:name()
end

local function get_public_header_manifest(target)
    if public_header_manifest then
        return public_header_manifest
    end

    local public_header_prefix = get_public_header_prefix(target)
    local manifest = {
        entries = {},
        by_source = {},
        by_basename = {},
    }

    for _, header in ipairs(os.files("src/**.h")) do
        local relative = path.relative(header, "src"):gsub("\\", "/")
        local source = normalize_path(header)
        local install = (public_header_prefix .. "/" .. relative):gsub("\\", "/")
        local entry = {
            relative = relative,
            source = source,
            install = install,
        }

        if manifest.by_source[source] then
            raise("duplicate public header source path: %s", source)
        end

        manifest.by_source[source] = entry
        table.insert(manifest.entries, entry)

        local basename = path.filename(header)
        local existing = manifest.by_basename[basename]
        if existing == nil then
            manifest.by_basename[basename] = entry
        elseif existing ~= false then
            manifest.by_basename[basename] = false
        end
    end

    public_header_manifest = manifest
    return manifest
end

local function resolve_public_header_entry(manifest, header, include_text)
    local current_dir_candidate = normalize_path(path.join(path.directory(header), include_text))
    if os.isfile(current_dir_candidate) then
        local entry = manifest.by_source[current_dir_candidate]
        if entry then
            return entry
        end
    end

    local source_root_candidate = normalize_path(path.join("src", include_text))
    if os.isfile(source_root_candidate) then
        local entry = manifest.by_source[source_root_candidate]
        if entry then
            return entry
        end
    end

    local basename = path.filename(include_text)
    local entry = manifest.by_basename[basename]
    if entry and entry ~= false then
        return entry
    end

    return nil
end

local function rewrite_public_header_content(target, manifest, header, content)
    local public_header_prefix = get_public_header_prefix(target)

    return (content:gsub('([ \t]*#include[ \t]+")([^"]+)(")', function(prefix, include_text, suffix)
        if include_text == "config.h" then
            return prefix .. public_header_prefix .. "/config.h" .. suffix
        end

        local entry = resolve_public_header_entry(manifest, header, include_text)
        if entry then
            return prefix .. entry.install .. suffix
        end

        return prefix .. include_text .. suffix
    end))
end

local function get_export_header_root(target)
    return path.join(path.directory(target:targetdir()), "export-headers")
end

local function export_public_headers(target)
    local manifest = get_public_header_manifest(target)
    local public_header_prefix = get_public_header_prefix(target)
    local export_root = get_export_header_root(target)
    local export_include_root = path.join(export_root, "include", public_header_prefix)

    os.tryrm(export_root)
    os.mkdir(export_include_root)

    for _, entry in ipairs(manifest.entries) do
        local rewritten = rewrite_public_header_content(target, manifest, entry.source, io.readfile(entry.source))
        local output = path.join(export_include_root, entry.relative)
        os.mkdir(path.directory(output))
        io.writefile(output, rewritten)
    end

    os.cp("$(builddir)/config/config.h", path.join(export_include_root, "config.h"))

    return {
        root = export_root,
        include_root = path.join(export_root, "include"),
        public_include_root = export_include_root,
    }
end

function install_export_public_headers(target, installdir)
    local public_header_prefix = get_public_header_prefix(target)
    local exportinfo = export_public_headers(target)
    local include_root = path.join(installdir, "include")

    os.mkdir(include_root)
    os.tryrm(path.join(include_root, public_header_prefix))
    os.cp(exportinfo.public_include_root, include_root)
end

function package_export_public_headers(target)
    local public_header_prefix = get_public_header_prefix(target)
    local exportinfo = export_public_headers(target)
    local include_root = path.join(target:packagedir(), "$(plat)/$(arch)/$(mode)/include")

    os.mkdir(include_root)
    os.tryrm(path.join(include_root, public_header_prefix))
    os.cp(exportinfo.public_include_root, include_root)
end
