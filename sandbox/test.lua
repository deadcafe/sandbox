local ffi = require("ffi")
local bit = require("bit")

function test()
	print("Hello world")
        print(eng_reset(1))
        print(eng_start(1))
        print(eng_stop(1))
        print(eng_poll(1))
end


--start

test()



--end