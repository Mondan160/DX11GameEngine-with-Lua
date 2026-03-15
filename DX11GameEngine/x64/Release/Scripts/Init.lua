print("Hello Lua!")

print("Generating Spiral Stairs...")

local total = 50
local radius = 5.0
local height_step = 0.5

local ErrorTex = "C:/Textures/Error.png"

for i = 1, total do
    local angle = i * 0.4
    local x = math.cos(angle) * radius
    local z = math.sin(angle) * radius
    local y = i * height_step

    spawn_cube(x, y, z + 20, 1.2, 0, -angle, 0, ErrorTex)
    print("Drawing...")
end

print("Spiral Complete!")