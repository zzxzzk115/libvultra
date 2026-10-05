local LightingController = {}

function LightingController:_ready()
    self.light = self.node:create_light_child("Script light", "point")
    self.light:set_position(0, 0, 2)
    local settings = self.light:light_settings()
    settings.red, settings.green, settings.blue = 0.2, 0.6, 1
    self.light:set_light_settings(settings)
    self.camera = self.node:create_camera_child("Script camera")
    self.camera:set_position(0, 0, 3)
    self.camera:make_current()
    self.material = self.scene:create_material("Script material")
    local parameters = self.material:parameters()
    parameters.base_green, parameters.base_blue = 0.25, 0.1
    self.material:set_parameters(parameters)
    self.node:get_child(0):set_material(0, self.material)
    self.environment = self.node:create_environment_child("Script environment")
    self.environment:make_environment_current()
end

function LightingController:_process(delta)
    local settings = self.light:light_settings()
    settings.intensity = 2 + delta
    self.light:set_light_settings(settings)
    local projection = self.camera:camera_settings()
    projection.vertical_fov = 0.9
    self.camera:set_camera_settings(projection)
    local parameters = self.material:parameters()
    parameters.base_red = 0.4 + delta
    self.material:set_parameters(parameters)
    self.environment:set_environment_settings({ intensity = 1 + delta })
end

return LightingController
