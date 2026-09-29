-- Cover art fetching for the metadata scraper.
--
-- Two sources, tried in order of how much they know about the specific dump:
--
--   IGDB               exact, but needs a Twitch client id and secret
--   libretro-thumbnails  no credentials, keyed on the No-Intro name
--
-- Both hand back raw image bytes, which the C bridge stores in
-- games.artwork_blob. Nothing is written to disk.

local http  = require("socket.http")
local ltn12 = require("ltn12")
local json  = require("json")

local M = {}

-- Covers are small. Anything past this is either the wrong URL or an error
-- page, and it is not going into a database row either way.
local MAX_COVER_BYTES = 8 * 1024 * 1024
local TIMEOUT_SECONDS = 15

local USER_AGENT = "MSE/1.0 (+https://github.com/mse)"

-- ==========================================
-- HTTP
-- ==========================================

local function request(spec)
    local sink_table = {}
    http.TIMEOUT = TIMEOUT_SECONDS

    local headers = spec.headers or {}
    headers["User-Agent"] = headers["User-Agent"] or USER_AGENT

    local ok, code, response_headers = http.request{
        url     = spec.url,
        method  = spec.method or "GET",
        headers = headers,
        source  = spec.body and ltn12.source.string(spec.body) or nil,
        sink    = ltn12.sink.table(sink_table),
    }

    if not ok then
        return nil, code
    end
    return table.concat(sink_table), code, response_headers
end

-- Percent-encodes a single path segment. Deliberately leaves the characters
-- that are legal in a path alone: libretro filenames are full of brackets,
-- commas and apostrophes, and encoding them all produces a URL the CDN
-- answers with a 404.
local UNSAFE = "[^%w%-%.%_%~%(%)%!%'%,%+%$]"

local function escape_path(segment)
    return (segment:gsub(UNSAFE, function(c)
        return string.format("%%%02X", c:byte())
    end))
end

-- The extension is unreliable (libretro serves .png, IGDB .jpg, and both lie
-- occasionally), so the type comes from the first few bytes.
local function sniff_mime(bytes)
    if not bytes or #bytes < 12 then return nil end

    if bytes:sub(1, 8) == "\137PNG\r\n\26\n" then return "image/png" end
    if bytes:sub(1, 3) == "\255\216\255" then return "image/jpeg" end
    if bytes:sub(1, 6) == "GIF89a" or bytes:sub(1, 6) == "GIF87a" then return "image/gif" end
    if bytes:sub(1, 4) == "RIFF" and bytes:sub(9, 12) == "WEBP" then return "image/webp" end
    if bytes:sub(1, 2) == "BM" then return "image/bmp" end

    return nil
end

-- Fetches a URL and returns it only if it really is an image.
local function download_image(url)
    local body, code = request{url = url}

    if not body then
        print(string.format("[COVER] %s: request failed (%s)", url, tostring(code)))
        return nil
    end
    if code ~= 200 then
        -- 404 is the normal answer for "this system has no thumbnail for that
        -- name", so it is not worth shouting about.
        if code ~= 404 then
            print(string.format("[COVER] %s: HTTP %s", url, tostring(code)))
        end
        return nil
    end
    if #body > MAX_COVER_BYTES then
        print(string.format("[COVER] %s: %d bytes is too large, skipping", url, #body))
        return nil
    end

    local mime = sniff_mime(body)
    if not mime then
        print(string.format("[COVER] %s: response is not an image", url))
        return nil
    end

    print(string.format("[COVER] %s: %d bytes, %s", url, #body, mime))
    return body, mime, url
end

-- ==========================================
-- SOURCE: libretro-thumbnails
-- ==========================================

-- The thumbnail repository is laid out by No-Intro system name. PlayMatch
-- reports the platform in its own words, so the two have to be mapped.
local LIBRETRO_SYSTEMS = {
    ["nintendo entertainment system"]         = "Nintendo - Nintendo Entertainment System",
    ["nes"]                                   = "Nintendo - Nintendo Entertainment System",
    ["famicom"]                               = "Nintendo - Nintendo Entertainment System",
    ["family computer"]                       = "Nintendo - Nintendo Entertainment System",
    ["super nintendo entertainment system"]   = "Nintendo - Super Nintendo Entertainment System",
    ["snes"]                                  = "Nintendo - Super Nintendo Entertainment System",
    ["super famicom"]                         = "Nintendo - Super Nintendo Entertainment System",
    ["nintendo 64"]                           = "Nintendo - Nintendo 64",
    ["game boy"]                              = "Nintendo - Game Boy",
    ["game boy color"]                        = "Nintendo - Game Boy Color",
    ["game boy advance"]                      = "Nintendo - Game Boy Advance",
    ["nintendo ds"]                           = "Nintendo - Nintendo DS",
    ["nintendo gamecube"]                     = "Nintendo - GameCube",
    ["gamecube"]                              = "Nintendo - GameCube",
    ["sega master system"]                    = "Sega - Master System - Mark III",
    ["master system"]                         = "Sega - Master System - Mark III",
    ["sega mega drive"]                       = "Sega - Mega Drive - Genesis",
    ["sega genesis"]                          = "Sega - Mega Drive - Genesis",
    ["mega drive"]                            = "Sega - Mega Drive - Genesis",
    ["genesis"]                               = "Sega - Mega Drive - Genesis",
    ["sega game gear"]                        = "Sega - Game Gear",
    ["game gear"]                             = "Sega - Game Gear",
    ["sega saturn"]                           = "Sega - Saturn",
    ["sega dreamcast"]                        = "Sega - Dreamcast",
    ["dreamcast"]                             = "Sega - Dreamcast",
    ["sony playstation"]                      = "Sony - PlayStation",
    ["playstation"]                           = "Sony - PlayStation",
    ["sony playstation portable"]             = "Sony - PlayStation Portable",
    ["playstation portable"]                  = "Sony - PlayStation Portable",
    ["atari 2600"]                            = "Atari - 2600",
    ["atari 7800"]                            = "Atari - 7800",
    ["atari lynx"]                            = "Atari - Lynx",
    ["turbografx-16"]                         = "NEC - PC Engine - TurboGrafx 16",
    ["pc engine"]                             = "NEC - PC Engine - TurboGrafx 16",
    ["neo geo pocket color"]                  = "SNK - Neo Geo Pocket Color",
    ["wonderswan"]                            = "Bandai - WonderSwan",
}

local function libretro_system(platform_name)
    if not platform_name or platform_name == "" then return nil end
    return LIBRETRO_SYSTEMS[platform_name:lower()]
end

-- libretro rewrites the characters that are awkward in a filename; the
-- thumbnail is stored under the rewritten name, so the same substitution has
-- to happen here before the URL is built.
local function libretro_filename(game_name)
    return (game_name:gsub("[&%*/:`<>%?\\|\"]", "_"))
end

function M.from_libretro(platform_name, game_name)
    local system = libretro_system(platform_name)
    if not system then
        print(string.format("[COVER] libretro has no system mapping for '%s'", tostring(platform_name)))
        return nil
    end
    if not game_name or game_name == "" then return nil end

    local url = string.format("https://thumbnails.libretro.com/%s/Named_Boxarts/%s.png",
                              escape_path(system), escape_path(libretro_filename(game_name) .. ""))

    print(string.format("[COVER] libretro: trying '%s' / '%s'", system, game_name))
    return download_image(url)
end

-- ==========================================
-- SOURCE: IGDB
-- ==========================================

-- Client-credentials token. IGDB tokens last weeks, but a scrape run is short
-- and the worker is not long-lived enough for caching to matter.
local function igdb_token(client_id, client_secret)
    local url = string.format(
        "https://id.twitch.tv/oauth2/token?client_id=%s&client_secret=%s&grant_type=client_credentials",
        escape_path(client_id), escape_path(client_secret))

    local body, code = request{url = url, method = "POST", headers = {["Content-Length"] = "0"}}
    if not body or code ~= 200 then
        print(string.format("[COVER] IGDB auth failed (HTTP %s)", tostring(code)))
        return nil
    end

    local ok, payload = pcall(json.decode, body)
    if not ok or not payload or not payload.access_token then
        print("[COVER] IGDB auth response could not be parsed")
        return nil
    end
    return payload.access_token
end

function M.from_igdb(igdb_game_id, client_id, client_secret)
    if not igdb_game_id or not client_id or client_id == "" or not client_secret or client_secret == "" then
        return nil
    end

    local token = igdb_token(client_id, client_secret)
    if not token then return nil end

    local query = string.format("fields image_id; where game = %s; limit 1;", tostring(igdb_game_id))
    local body, code = request{
        url     = "https://api.igdb.com/v4/covers",
        method  = "POST",
        body    = query,
        headers = {
            ["Client-ID"]      = client_id,
            ["Authorization"]  = "Bearer " .. token,
            ["Accept"]         = "application/json",
            ["Content-Type"]   = "text/plain",
            ["Content-Length"] = tostring(#query),
        },
    }

    if not body or code ~= 200 then
        print(string.format("[COVER] IGDB cover lookup failed (HTTP %s)", tostring(code)))
        return nil
    end

    local ok, payload = pcall(json.decode, body)
    if not ok or type(payload) ~= "table" or not payload[1] or not payload[1].image_id then
        print("[COVER] IGDB returned no cover for game " .. tostring(igdb_game_id))
        return nil
    end

    -- t_cover_big is 264x374; t_1080p exists but is far larger than anything
    -- the library grid will ever draw.
    local url = string.format("https://images.igdb.com/igdb/image/upload/t_cover_big/%s.jpg", payload[1].image_id)
    print("[COVER] IGDB: " .. url)
    return download_image(url)
end

-- ==========================================
-- ENTRY POINT
-- ==========================================

-- Returns bytes, mime, url. IGDB first when credentials were configured,
-- because it knows the actual release; libretro otherwise, and as the fallback
-- when IGDB has nothing.
function M.fetch(opts)
    if opts.igdb_id and opts.client_id and opts.client_id ~= "" then
        local bytes, mime, url = M.from_igdb(opts.igdb_id, opts.client_id, opts.client_secret)
        if bytes then return bytes, mime, url end
    end

    return M.from_libretro(opts.platform_name, opts.game_name)
end

return M
