-- MAME autoboot script for the demo. It loads the tape through the 128K menu
-- (TYPE_TEXT typed at 3 s, tape started), then presses the demo keys through
-- the emulated keyboard matrix and saves screenshots.
-- Environment:
--   TYPE_TEXT   text for the keyboard buffer: "\r" for the 128K menu (Tape
--               Loader), "\r" also for the +3 menu
--   START       emulated second when the demo is running (default 240)
--   MANIFEST    file that receives one line per screenshot, in order
local text = (os.getenv("TYPE_TEXT") or "\r"):gsub("\\r", "\r")
local start = tonumber(os.getenv("START") or "240")
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

add(-start + 3.0, function() manager.machine.natkeyboard:post(text)
                             local c = manager.machine.cassettes[":cassette"]
                             if c then c:play() end end)
frames(0.0,  "start: Gigascreen picture, flipping", 3)
press(1.0, "4 "); frames(1.5, "4 primary (bank 5 only)", 1)
press(2.0, "5 "); frames(2.5, "5 secondary (bank 7 only)", 1)
press(3.0, "6 "); press(3.5, "m "); frames(4.0, "6 flip on, m menu shown", 3)
press(5.0, "7 "); press(5.5, "8 "); press(6.0, "8 "); press(6.5, "9 ")
frames(7.0, "7,8,8,9: giga Off(0), mode Double, top Shad", 1)
press(8.0, "m "); press(8.5, "2 "); frames(9.0, "2 double-high picture, menu hidden", 3)
press(10.0, "4 "); frames(10.5, "4 double-high bank 5 only (even lines)", 1)
press(11.0, "5 "); frames(11.5, "5 double-high bank 7 only (odd lines)", 1)
press(12.0, "3 "); frames(13.0, "3 ULAplus picture (MAME: no palette)", 1)
press(14.0, "m "); frames(14.5, "m menu on ULAplus picture", 1)
press(15.0, "0 "); frames(15.5, "0 ULAplus off", 1)
press(16.0, "m "); press(16.5, "1 "); frames(17.5, "1 back to the Gigascreen picture", 3)
press(19.0, "q "); frames(20.0, "q quit to BASIC", 1)

table.sort(events, function(a, b) return a[1] < b[1] end)
local n = 1
emu.register_frame_done(function()
  local now = manager.machine.time:as_double()
  while n <= #events and now >= events[n][1] do
    events[n][2]()
    n = n + 1
  end
end)
