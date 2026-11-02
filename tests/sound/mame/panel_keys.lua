-- MAME autoboot script for the panel test. It loads the tape like drive.lua
-- (TYPE_TEXT typed at 3 s, tape started), then presses keys through the
-- emulated keyboard matrix, so it can send CAPS SHIFT with 6 and 7.
-- Environment: TYPE_TEXT, as in drive.lua.
local text = (os.getenv("TYPE_TEXT") or "\r"):gsub("\\r", "\r")

local function field(prefix)
  for tag, p in pairs(manager.machine.ioport.ports) do
    if tag:find("LINE") then
      for name, f in pairs(p.fields) do
        if name:sub(1, #prefix) == prefix then return f end
      end
    end
  end
  error("no key " .. prefix)
end

local function press(...)
  for _, k in ipairs({...}) do field(k):set_value(1) end
end
local function release(...)
  for _, k in ipairs({...}) do field(k):set_value(0) end
end

-- {time, function}
local events = {
  {3.0,  function() manager.machine.natkeyboard:post(text)
                    local c = manager.machine.cassettes[":cassette"]
                    if c then c:play() end end},
  {34.0, function() manager.machine.video:snapshot() end},
  {34.5, function() press("CAPS SHIFT", "6 ") end},
  {34.62, function() release("CAPS SHIFT", "6 ") end},
  {35.0, function() manager.machine.video:snapshot() end},   -- down once
  {35.5, function() press("CAPS SHIFT", "6 ") end},
  {37.0, function() release("CAPS SHIFT", "6 ") end},
  {37.5, function() manager.machine.video:snapshot() end},   -- held: repeats
  {38.0, function() press("CAPS SHIFT", "7 ") end},
  {38.12, function() release("CAPS SHIFT", "7 ") end},
  {38.5, function() manager.machine.video:snapshot() end},   -- up once
  {39.0, function() press("ENTER") end},
  {39.12, function() release("ENTER") end},
  {39.5, function() manager.machine.video:snapshot() end},   -- toggled
}
local n = 1
emu.register_frame_done(function()
  local now = manager.machine.time:as_double()
  while n <= #events and now >= events[n][1] do
    events[n][2]()
    n = n + 1
  end
end)
