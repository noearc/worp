
local jack_c = require "jack_c"

--
-- Register jack audio ports, n_in input ports and n_out output ports.
-- Processing is done in given fn, all input data is given as arguments, all
-- returned data is passed as output data.
--

local function jack_dsp(jack, name, n_in, n_out, fn)

	local group = jack.group_list[name]

	if not group then

		group = {
			fn = fn,
			fn_ok = function() end
		}
		jack.group_list[name] = group

		local t = 0
		local dt = 1/jack.srate
		local gu, fd = jack_c.add_group(jack.j, name, n_in, n_out)

		-- This function gets called when the jack thread needs more data. The
		-- many calls into C should probably be optimized at some time

		watch_fd(fd, function()
			p.read(fd, 1)
			local ok = safecall(function()
				for i = 1, jack.bsize do
					jack_c.write(gu, group.fn(t, jack_c.read(gu)))
					t = t + dt
				end
			end)
			if not ok then
				print("Restoring last known good function")
				group.fn = group.fn_ok
			end
		end)

	else
		group.fn_ok = group.fn
		group.fn = fn
	end

end



--
-- Register jack midi port with given name. Received midi events are passed to
-- callback function
--

local function jack_midi(jack, name, fn)

	local midi_msg = {
		[0x90] = "noteon",
		[0x80] = "noteoff",
		[0xb0] = "cc",
		[0xc0] = "pc",
	}

	local fd = jack_c.add_midi(jack.j, name)

	watch_fd(fd, function()
		local msg = p.read(fd, 3)

		local b1, b2, b3 = string.byte(msg, 1, #msg)

		local t = bit.band(b1, 0xf0)
		t = midi_msg[t] or t
		local channel = bit.band(b1, 0x0f) + 1
		fn(channel, t, b2, b3)
		
	end)

end


local function new(name, port_list, fn)
	
	local j, srate, bsize = jack_c.open(name or "worp")

	local jack = {

		-- methods

		dsp = jack_dsp,
		midi = jack_midi,
		autoconnect = function(_, p1)
			return jack_c.autoconnect(j, p1)
		end,
		connect = function(_, p1, p2)
			return jack_c.connect(j, p1, p2)
		end,
		disconnect = function(_, p1, p2)
			return jack_c.disconnect(j, p1, p2)
		end,

		-- data
	
		j = j,
		srate = srate,
		bsize = bsize,
		group_list = {},

	}

	return jack

end


return {
   new = new
}

-- vi: ft=lua ts=3 sw=3

