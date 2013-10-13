#!/usr/bin/luajit

package.path = package.path .. ";./lib/?.lua"
package.cpath = package.cpath .. ";./lib/?.so"
require "strict"

p = require "posix"

env = setmetatable({}, { __index = _G })
local ev_queue = {}
local t_start = nil
local fds = {}

t_now = 0

-- 
-- Safe call: calls the given function with an error handler. Error traces
-- are rewritten to fixup the line numbers in de loaded chunk for chunks
-- that were loaded with the 'load_code()' function below.
--

function safecall(fn, ...)
	local function errhandler(err)
		local msg = debug.traceback("Error: " .. err, 3)
		msg = msg:gsub('%[string "live (.-):(%d+)"]:(%d+)', function(n, l1, l2)
			return n .. ":" .. (l1+l2-1)
		end)
		print(msg)
	end
	if type(fn) == "string" then fn = env[fn] end
	return xpcall(fn, errhandler, ...)
end


--
-- Load and run the given lua source 
--

local function load_code(code, name)
	local fn, err = load(code, name, "t", env)
	if fn then
		t_now = time()
		return safecall(fn)
	else
		return false, err
	end
end


--
-- Return monotonic time, starts at zero at first invocation
--

function time()
	local s, ns = p.clock_gettime(p.CLOCK_MONOTONIC)
	local t = s + ns / 1e9
	t_start = t_start or t
	return t - t_start
end


--
-- Schedule function 'fn' to be called in 't' seconds. 'fn' can be as string,
-- which will be resolved in the 'env' table at calling time
--

function at(t, fn, ...)
	local ev = {
		t_when = t_now + t,
		fn = fn,
		args = { ... }
	}
	table.insert(ev_queue, ev)
	table.sort(ev_queue, function(a, b)
		return a.t_when > b.t_when
	end)
end


--
-- Clear all events from the event queue 
--

function stop()
	ev_queue = {}
end


--
-- Play a note for the given duration using the given sound generator
--

function play(fn, note, vol, dur)
	fn(true, note, vol or 127)
	at(dur * 0.99, function()
		fn(false, note, vol)
	end)
end


--
-- Register the given file descriptor to the main poll() loop. 'fn' is called
-- when new data is available on the fd
--

function watch_fd(fd, fn)
	fds[fd] = { events = { IN = true }, fn = fn }
end


-- 
-- Open an UDP socket to receive Lua code chunks, and register to the 
-- mail loop.
--

local s = p.socket(p.AF_INET, p.SOCK_DGRAM, 0)
p.bind(s, { family = p.AF_INET, port = 9889, addr = "127.0.0.1" })

watch_fd(s, function()
	local code = p.recv(s, 65535)
	local from, to, name = 1, 1, "?"
	local f, t, n = code:match("\n%-%- live (%d+) (%d+) ([^\n]+)")
	if f then from, to, name = f, t, n end
	local ok, err = load_code(code, "live " .. name .. ":" .. from)
end)

p.signal(p.SIGINT, os.exit)

--
-- Run any files passed as arguments
--

for _, fname in ipairs(arg) do
	load_code(io.open(fname):read("*a"), fname .. ":1")
end

math.randomseed(os.time())
local t_start = time()
t_now = 0


--
-- Main loop: wait for timers or events and schedule callback functions
--

print "Ready"

while true do

	local dt = 10
	local ev = ev_queue[#ev_queue]

	if ev then
		dt = math.min(ev.t_when - time())
	end

	if dt > 0 then
		local r, a = p.poll(fds, dt * 1000)
		if r and r > 0 then
			for fd in pairs(fds) do
				if fds[fd].revents and fds[fd].revents.IN then
					t_now = time()
					safecall(fds[fd].fn)
				end
			end
		end
	end

	while ev and time() > ev.t_when do
		table.remove(ev_queue)
		t_now = ev.t_when
		safecall(ev.fn, unpack(ev.args))
		ev = ev_queue[#ev_queue]
	end

end

-- vi: ft=lua ts=3 sw=3
