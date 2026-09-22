local http = require("socket.http")
local ltn12 = require("ltn12")
local md5 = require("md5")
local inspect = require("inspect")
local json = require("json") -- Swapped from cjson to pure-Lua json.lua

-- Helper to extract the full file name with extension from a path
local function get_filename_with_ext(path)
    return path:match("^.+/(.+)$") or path:match("^.+\\(.+)$") or path
end

local function compute_rom_fingerprints(path)
    local f = io.open(path, "rb")
    if not f then return nil, 0, "" end
    
    local current = f:seek()
    local size = f:seek("end")
    f:seek("set", current)
    
    local content = f:read("*a")
    f:close()
    
    local file_md5 = string.lower(md5.sumhexa(content))
    return file_md5, size
end

-- ==========================================
-- STRATEGY 1: PLAYMATCH API ENGINE 
-- ==========================================
local function scrape_via_playmatch(rom_path, file_md5, file_size)
    local file_name = get_filename_with_ext(rom_path)
    print(string.format("[PLAYMATCH] Identifying: '%s' (%d bytes, MD5: %s)", file_name, file_size, file_md5))

    local encoded_name = string.gsub(file_name, " ", "%%20")
    
    local url = string.format(
        "https://playmatch.retrorealm.dev/api/identify/relations?fileName=%s&fileSize=%d&md5=%s",
        encoded_name,
        file_size,
        file_md5
    )
    
    print("[PLAYMATCH] Requesting: " .. url)

    local response_body = {}
    local _, code, _ = http.request{
        url = url,
        method = "GET",
        headers = { ["Accept"] = "application/json" },
        sink = ltn12.sink.table(response_body)
    }
    
    print("[PLAYMATCH] Response code: " .. tostring(code))

    if code ~= 200 then
        print(string.format("[PLAYMATCH] API Request failed. Status code: %s", tostring(code)))
        return nil
    end

    local json_str = table.concat(response_body)
    if #json_str == 0 then return nil end

    -- Parse JSON safely using json.lua
    local success, payload = pcall(json.decode, json_str)
    if not success or not payload then
        print("[PLAYMATCH] Failed to parse JSON response.")
        return nil
    end

    if payload.game and payload.game.name then
        print(string.format("[PLAYMATCH] Match found: '%s' (Playmatch ID: %s)", 
            payload.game.name, payload.game.id))
    end

    -- Return the entire structured payload so the orchestration function has everything 
    -- (Company, Platform, Hashes, Metadata) for the new SQLite schema.
    return payload
end

-- ==========================================
-- STRATEGY 2: SCREEN SCRAPER (INFRASTRUCTURE)
-- ==========================================
local function scrape_via_screenscraper(provider_id, auth_user, auth_pass)
    print("[INFRASTRUCTURE] ScreenScraper called with Provider ID: " .. tostring(provider_id))
    return 0, nil 
end

-- ==========================================
-- STRATEGY 3: IGDB ENGINE (INFRASTRUCTURE)
-- ==========================================
local function scrape_via_igdb(provider_id, client_id, client_secret)
    print("[INFRASTRUCTURE] IGDB called with Provider ID: " .. tostring(provider_id))
    return 0, nil
end

-- ==========================================
-- PUBLIC ORCHESTRATION PIPELINE ENTRYPOINT
-- ==========================================
function dispatch_async_scrape_request(rom_path, scraper_id, auth_token_a, auth_token_b)
    local file_md5, file_size = compute_rom_fingerprints(rom_path)
    if not file_md5 then 
        print("[WORKER] Failed to extract physical file parameters from target path: " .. tostring(rom_path))
        return 
    end

    print(string.format("[WORKER] Dispatching scrape request for: '%s' (MD5: %s, Size: %d bytes)", 
        rom_path, file_md5, file_size))

    -- 1. Use PlayMatch as the router to get the unified relational payload
    local pm_data = scrape_via_playmatch(rom_path, file_md5, file_size)
    
    if not pm_data or not pm_data.game then
        print("[WORKER ENGINE] No remote matching updates resolved for: " .. rom_path)
        return
    end

    -- 2. Extract external provider IDs dynamically from the array
    local igdb_id, ss_id
    if pm_data.externalMetadata then
        for _, provider in ipairs(pm_data.externalMetadata) do
            if provider.providerName == "IGDB" and provider.providerId then
                igdb_id = provider.providerId
            elseif provider.providerName == "ScreenScraper" and provider.providerId then
                ss_id = provider.providerId
            end
        end
    end

    -- 3. Enrich the data using the exact IDs PlayMatch gave us
    local final_year = 0
    local final_artwork = nil

    if igdb_id then
        final_year, final_artwork = scrape_via_igdb(igdb_id, auth_token_a, auth_token_b)
    elseif ss_id then
        final_year, final_artwork = scrape_via_screenscraper(ss_id, auth_token_a, auth_token_b)
    end
    
    print(string.format("[WORKER ENGINE] Committing rich metadata updates for: '%s'", pm_data.game.name))
    
    -- FIX 5: Uncomment. Map the table data out into specific native scaler values so `luaL_checkstring(L, 2)` succeeds
    print(string.format("[WORKER ENGINE] Committing rich metadata updates for: '%s'", pm_data.game.name))
    
    -- Extract platform and company safely, falling back to empty strings if missing
    local p_id   = (pm_data.platform and pm_data.platform.id) or ""
    local p_name = (pm_data.platform and pm_data.platform.name) or ""
    
    local c_id   = (pm_data.company and pm_data.company.id) or ""
    local c_name = (pm_data.company and pm_data.company.name) or ""
    
    -- Push the entire normalized relational dataset across the C Bridge
    update_game_db_record(
        rom_path,                          -- 1
        file_size,                         -- 2
        file_md5,                          -- 3
        pm_data.game.id,                   -- 4
        pm_data.game.name or "Unknown",    -- 5
        p_id,                              -- 6
        p_name,                            -- 7
        c_id,                              -- 8
        c_name,                            -- 9
        final_year or 0,                   -- 10
        final_artwork                      -- 11
    )
end