-- MAME autoboot script for the panel test, GIGASCREEN page. It loads the tape
-- like drive.lua (TYPE_TEXT typed at 3 s, tape started), then presses keys
-- through the emulated keyboard matrix and saves a screenshot after each one.
-- At each screenshot it prints the four bytes of the panel's own state
-- table (devices, border, display, debug) as "STATE xx xx xx xx".
-- Environment: TYPE_TEXT, as in drive.lua. STATE_ADDR, the address of the
-- label "state" in build/lst/panel-tap.lst, as a hex number without prefix.
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

-- Key names as MAME gives them (the start of the field name).
local function tap(...)
  local keys = {...}
  return function() for _, k in ipairs(keys) do field(k):set_value(1) end end,
         function() for _, k in ipairs(keys) do field(k):set_value(0) end end
end

local state_addr = tonumber(os.getenv("STATE_ADDR") or "", 16)
local events = {
  {3.0, function() manager.machine.natkeyboard:post(text)
                   local c = manager.machine.cassettes[":cassette"]
                   if c then c:play() end end},
}
local t = 34.0
local function snap(label)
  events[#events + 1] = {t, function()
    print("SNAP " .. label)
    if state_addr then
      local m = manager.machine.devices[":maincpu"].spaces["program"]
      print(string.format("STATE %02X %02X %02X %02X", m:read_u8(state_addr),
            m:read_u8(state_addr + 1), m:read_u8(state_addr + 2), m:read_u8(state_addr + 3)))
    end
    manager.machine.video:snapshot() end}
  t = t + 0.5
end
local function key(label, ...)
  local down, up = tap(...)
  events[#events + 1] = {t, down}
  events[#events + 1] = {t + 0.15, up}
  t = t + 0.6
  snap(label)
end

snap("main")
key("g opens", "g ")
key("1 detection", "1 ")
key("2 locked", "2 ")
key("3 mode crt", "3 ")
key("3 mode double", "3 ")
key("3 mode 50/50", "3 ")
key("3 mode crt", "3 ")
key("3 mode double", "3 ")
key("4 method", "4 ")
key("5 first bank", "5 ")
key("cursor down", "CAPS SHIFT", "6 ")
key("cursor up", "CAPS SHIFT", "7 ")
key("enter on row 1", "ENTER")
key("r defaults", "r ")
key("s send all", "s ")
key("5 first bank", "5 ")
key("g closes", "g ")
key("7 border", "7 ")
key("g opens again", "g ")
key("space closes", "SPACE")
key("g opens a third time", "g ")
key("break closes", "CAPS SHIFT", "SPACE")

local n = 1
emu.register_frame_done(function()
  local now = manager.machine.time:as_double()
  while n <= #events and now >= events[n][1] do
    events[n][2]()
    n = n + 1
  end
end)
