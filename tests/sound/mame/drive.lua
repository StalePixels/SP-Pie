-- MAME autoboot script: starts the tape and types the keys that load it.
-- Environment:
--   TYPE_AT    emulated second at which to type TYPE_TEXT (for example 3)
--   TYPE_TEXT  text for the keyboard buffer: "\r" for the 128K menu
--              (Tape Loader), 'j""\r' for 48K BASIC (LOAD "")
--   KEYS_AT    emulated second at which to type KEYS_TEXT after the load
--              (for example to press keys in a menu program)
--   SNAP_AT    comma list of emulated seconds at which to save a screenshot
local times = {}
for t in string.gmatch(os.getenv("SNAP_AT") or "", "[^,]+") do times[#times+1] = tonumber(t) end
local type_at = tonumber(os.getenv("TYPE_AT") or "")
local text = os.getenv("TYPE_TEXT") or "\r"
text = text:gsub("\\r", "\r")
local keys_at = tonumber(os.getenv("KEYS_AT") or "")
local keys = (os.getenv("KEYS_TEXT") or ""):gsub("\\r", "\r")
local idx = 1
emu.register_frame_done(function()
  local now = manager.machine.time:as_double()
  if type_at and now >= type_at then
    manager.machine.natkeyboard:post(text)
    local c = manager.machine.cassettes[":cassette"]
    if c then c:play() end
    type_at = nil
  end
  if keys_at and now >= keys_at then
    manager.machine.natkeyboard:post(keys)
    keys_at = nil
  end
  if idx <= #times and now >= times[idx] then
    manager.machine.video:snapshot()
    idx = idx + 1
  end
end)
