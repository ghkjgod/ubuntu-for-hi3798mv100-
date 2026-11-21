-- Loaded only for the Hi3798MV100 decoder and when the user enables hardware decoding.
local mp = require "mp"
local sysfs = "/sys/devices/platform/mv100-vdh/"
local function counter(name)
    local f = io.open(sysfs .. name, "r")
    if not f then return nil end
    local n = tonumber(f:read("*l"))
    f:close()
    return n
end

local watching = false
local initial, last_frames, last_time
local paused_on_load
local function software()
    mp.set_property("file-local-options/vd", "h264")
    mp.set_property("file-local-options/hwdec", "no")
end

mp.add_hook("on_preloaded", 45, function()
    watching = false
    paused_on_load = mp.get_property_bool("pause", false)
    local tracks = mp.get_property_native("track-list") or {}
    for _, t in ipairs(tracks) do
        if t.type == "video" and t.codec == "h264" and not t.albumart then
            if (t["demux-w"] or 0) > 1920 or (t["demux-h"] or 0) > 1088 then
                software()
                return
            end
            initial = counter("avc_refusals")
            last_frames = counter("decoded_frames")
            last_time = nil
            watching = initial ~= nil
            return
        end
    end
end)

mp.add_periodic_timer(0.3, function()
    if not watching then return end
    local count = counter("decoded_frames")
    local t = mp.get_property_number("time-pos")
    if count ~= last_frames then
        last_frames = count
        if t then last_time = t end
    end
    if counter("avc_refusals") == initial then return end
    watching = false
    local vid = mp.get_property("vid")
    if not vid or vid == "no" then return end
    -- A refused short stream can reach keep-open EOF before the timer fires.
    local resume = not paused_on_load and mp.get_property_bool("eof-reached", false)
    mp.msg.warn("VDH declined the stream, switching to FFmpeg H.264")
    mp.set_property("vid", "no")
    software()
    mp.set_property("vid", vid)
    if mp.get_property_bool("seekable", false) then
        mp.commandv("seek", tostring(last_time or 0), "absolute+exact")
    end
    if resume then mp.set_property_bool("pause", false) end
end)
mp.register_event("end-file", function() watching = false end)
