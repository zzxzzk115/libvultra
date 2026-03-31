Y = 0
Elapsed = 0

function OnCreate(self)
    print("test_move.lua attached")
    Y = self.transform.position.y
    Elapsed = 0
end

function OnUpdate(self, dt)
    Elapsed = Elapsed + dt
    local t = self.transform
    local pos = t.position
    pos.y = Y + math.sin(Elapsed * 2.0) * 0.1
    t.position = pos
end
