local openssl = require("openssl")
local random = openssl.random
local function v4()
    local bytes = random(16)
    local t = {}

    for i = 1, 16 do
        t[i] = string.format("%02x", string.byte(bytes, i))
    end

    local hex = table.concat(t)

    local variant_byte = string.byte(bytes, 9)
    local variant_hex = string.format("%x", 8 + (variant_byte % 4))

    hex = hex:sub(1, 12) .. "4" .. hex:sub(14, 15) ..
          variant_hex .. hex:sub(18, 32)

    return string.format("%s-%s-%s-%s-%s",
        hex:sub(1, 8), hex:sub(9, 12), hex:sub(13, 16),
        hex:sub(17, 20), hex:sub(21, 32))
end


return {
    v4 = v4
}