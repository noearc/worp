#!/usr/bin/lua

package.path = package.path .. ";./lib/?.lua"
package.cpath = package.cpath .. ";./lib/?.so"
local p = require("posix")

local function send(code)
	fd = p.socket(p.AF_INET, p.SOCK_DGRAM, 0)
	p.sendto(fd, code, { family = p.AF_INET, addr = "127.0.0.1", port = 9889 })
	p.close(fd)
end

function worp(what)
	local b = vim.api.nvim_buf_get_lines(0, 0, -1, false)

	local from = b[1]
	local to = b[#b]

	if what == "stop" then
		from, to = 0, 0
		send("stop()")
	elseif what == "all" then
		from = 1
		to = #b
	elseif what == "paragraph" then
		while from > 1 and b[from - 1]:find("%S") do
			from = from - 1
		end
		while to < #b and b[to + 1]:find("%S") do
			to = to + 1
		end
	elseif what == "function" then
		while from > 1 and not b[from]:find("^function") do
			from = from - 1
		end
		while to < #b and not b[to]:find("^end") do
			to = to + 1
		end
	end

	vim.cmd("sign define sent text=┆ texthl=nonText")
	vim.cmd("sign unplace *")

	if from > 0 then
		local code = {}
		for i = from, to do
			vim.cmd("sign place " .. i .. " line=" .. i .. " name=sent file=" .. b.fname)
			code[#code + 1] = b[i]
		end
		code[#code + 1] = "-- live " .. from .. " " .. to .. " " .. (b.name or "")
		send(table.concat(code, "\n"))
	else
		vim.cmd("sign place 1 line=1 name=sent file=" .. b.fname)
	end
end

vim.cmd(':noremap ,a :lua worp("all")<CR>')
vim.cmd(':noremap ,f :lua worp("function")<CR>')
vim.cmd(':noremap ,p :lua worp("paragraph")<CR>')
vim.cmd(':noremap ,v :lua worp("visual")<CR>')
vim.cmd(':noremap ,, :lua worp("stop")<CR>')
vim.cmd(':noremap <CR> :lua worp("line")<CR>')

-- vi: ft=lua ts=3 sw=3
