Y = 0

function OnCreate(self)
    print("test_move.lua attached")
    Y = self.transform.position.y
end

function OnUpdate(self, dt)
    local t = self.transform
    local pos = t.position
    local time = os.clock()
    pos.y = Y + math.sin(time) * 0.1
    t.position = pos
end