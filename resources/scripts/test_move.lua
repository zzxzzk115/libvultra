Y = 0

function OnCreate(self)
	print("test_move.lua attached")
	Y = self:get_position().y
end

function OnUpdate(self, dt)
	-- move up and down
	local pos = self:get_position()
	local time = os.clock()
	pos.y = Y + math.sin(time) * 0.1
	self:set_position(pos)
end
