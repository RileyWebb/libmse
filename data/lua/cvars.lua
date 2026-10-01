-- libmse CVar binding for Lua api

local ffi = require("ffi")

ffi.cdef[[
    typedef struct libmse_cvar_s libmse_cvar_t;

    typedef void (*libmse_cvar_change_cb)(libmse_cvar_t* cvar, void* user_data);

    typedef enum libmse_cvar_type_e {
        LIBMSE_CVAR_INT,
        LIBMSE_CVAR_FLOAT,
        LIBMSE_CVAR_DOUBLE,
        LIBMSE_CVAR_STRING,
        LIBMSE_CVAR_VEC2,
        LIBMSE_CVAR_VEC3,
        LIBMSE_CVAR_VEC4,
    } libmse_cvar_type_t;

    typedef struct libmse_cvar_s {
        const char* name;
        const char* description;
        libmse_cvar_type_t type;

        union {
            int *i;
            float *f;
            double *d;
            const char** s;
            float *v;
        } data;

        char* alloc_s;

        libmse_cvar_change_cb cb;
        void* user_data;

        /* Storage the cvar system owns, for a cvar that was defined rather
           than bound to somebody else's memory. Appended to the end of the
           struct, so everything above keeps its offset. */
        union {
            int i;
            float f;
            double d;
            const char* s;
            float v[4];
        } storage;

        bool owned;
    } libmse_cvar_t;

    void* libmse_get_cvar_registry();
    size_t libmse_get_cvar_count();

    bool libmse_cvar_set_i(const char* name, int value);
    bool libmse_cvar_set_f(const char* name, float value);
    bool libmse_cvar_set_d(const char* name, double value);
    bool libmse_cvar_set_s(const char* name, const char *value);

    bool libmse_cvar_register(const char *name, libmse_cvar_type_t type, void *ref, const char *description);
]]

local lib = ffi.load("libmse") 

local function find_cvar(name)
    local count = tonumber(lib.libmse_get_cvar_count())
    local registry = ffi.cast("libmse_cvar_t**", lib.libmse_get_cvar_registry())
    
    for i = 0, count - 1 do
        if registry[i] ~= nil and ffi.string(registry[i].name) == name then
            return registry[i]
        end
    end
    return nil
end

local cvars = {}

setmetatable(cvars, {
    -- Reading a CVar: e.g., local val = cvars.my_var
    __index = function(_, name)
        local cvar = find_cvar(name)
        if not cvar then return nil end

        local t = cvar.type
        if t == lib.LIBMSE_CVAR_INT then
            return cvar.data.i[0]
        elseif t == lib.LIBMSE_CVAR_FLOAT then
            return cvar.data.f[0]
        elseif t == lib.LIBMSE_CVAR_DOUBLE then
            return cvar.data.d[0]
        elseif t == lib.LIBMSE_CVAR_STRING then
            -- Safely dereference char** to a Lua string
            if cvar.data.s ~= nil and cvar.data.s[0] ~= nil then
                return ffi.string(cvar.data.s[0])
            end
            return ""
        end
        return nil
    end,

    -- Writing a CVar: e.g., cvars.my_var = 45
    __newindex = function(_, name, value)
        local cvar = find_cvar(name)
        if not cvar then 
            local dummy_ref = ffi.cast("void*", ffi.cast("const char*", ""))
            
            lib.libmse_cvar_register(name, lib.LIBMSE_CVAR_STRING, dummy_ref, "Auto-registered from Lua")

            cvar = find_cvar(name)
            if not cvar then 
                error("Failed to auto-register CVar: " .. tostring(name)) 
            end
        end

        local t = cvar.type
        if t == lib.LIBMSE_CVAR_INT then
            lib.libmse_cvar_set_i(name, tonumber(value) or 0)

        elseif t == lib.LIBMSE_CVAR_FLOAT then
            lib.libmse_cvar_set_f(name, tonumber(value) or 0.0)

        elseif t == lib.LIBMSE_CVAR_DOUBLE then
            lib.libmse_cvar_set_d(name, tonumber(value) or 0.0)

        elseif t == lib.LIBMSE_CVAR_STRING then
            lib.libmse_cvar_set_s(name, tostring(value))
        end
    end
})

return cvars