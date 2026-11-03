-- MAME autoboot script for the demo. It loads the tape through the 128K menu
-- (TYPE_TEXT typed at 3 s, tape started), then presses the demo keys through
-- the emulated keyboard matrix and saves screenshots.
-- Environment:
--   TYPE_TEXT   text for the keyboard buffer: "\r" for the 128K menu (Tape
--               Loader), "\r" also for the +3 menu
--   START       emulated second when the demo is running (default 30)
--   MANIFEST    file that receives one line per screenshot, in order
local text = (os.getenv("TYPE_TEXT") or "\r"):gsub("\\r", "\r")
local start = tonumber(os.getenv("START") or "30")
local manifest = io.open(os.getenv("MANIFEST") or "demo-keys.manifest", "w")

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
local function key(k) return {function() field(k):set_value(1) end,
                              function() field(k):set_value(0) end} end
local function snap(label)
  return function()
    manager.machine.video:snapshot()
    manifest:write(label .. "\n"); manifest:flush()
  end
end

-- {seconds after start, action}. A key is pressed at t and released at t+0.15.
local events = {}
local function add(t, f) events[#events + 1] = {start + t, f} end
local function press(t, k)
  local kk = key(k)
  add(t, kk[1]); add(t + 0.15, kk[2])
end
local function frames(t, label, n)       -- n screenshots on consecutive frames
  for i = 1, n do add(t + (i - 1) * 0.02, snap(label .. " frame " .. i)) end
end

add(-30 + 3.0, function() manager.machine.natkeyboard:post(text)
                          local c = manager.machine.cassettes[":cassette"]
                          if c then c:play() end end)
frames(0.0, "start: picture 0, primary, menu", 1)
press(1.0, "2 ");  frames(1.5, "2 secondary", 1)
press(2.0, "3 ");  frames(2.5, "3 flip on", 3)
press(3.5, "1 ");  frames(4.0, "1 primary, flip off", 1)
press(4.5, "4 ");  press(5.0, "5 ");  press(5.5, "5 ")
press(6.0, "6 ");  press(6.5, "4 ")
frames(7.0, "4,5,5,6,4: giga Off... mode Double, top Shad", 1)
press(8.0, "7 ");  frames(8.5, "7 ULAplus on (MAME: no change)", 1)
press(9.0, "8 ");  frames(9.5, "8 picture ULAplus (ULA+ on)", 1)
press(10.0, "7 "); frames(10.5, "7 ULAplus off", 1)
press(11.0, "m "); frames(11.5, "m menu hidden", 1)
press(12.0, "m "); frames(12.5, "m menu shown", 1)
press(13.0, "8 "); press(13.5, "2 "); frames(14.0, "8 picture 0 again, 2 secondary", 1)
press(15.0, "q "); frames(16.0, "q quit to BASIC", 1)

table.sort(events, function(a, b) return a[1] < b[1] end)
local n = 1
emu.register_frame_done(function()
  local now = manager.machine.time:as_double()
  while n <= #events and now >= events[n][1] do
    events[n][2]()
    n = n + 1
  end
end)
