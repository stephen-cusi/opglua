-- GLua syntax corpus check: parse every .lua under the reference game's
-- lua/ and gamemodes/ trees.  Uses loadfile so the compiler runs; nothing
-- is executed.  Reports total / passed / per-file failures.
local roots = {
  "D:/games/garrysmod/garrysmod/lua",
  "D:/games/garrysmod/garrysmod/gamemodes",
}
local total, failed, failures = 0, 0, {}
local function walk(dir)
  local p = io.popen('dir /b /s /a-d "'..dir..'" 2>nul')
  if not p then return end
  for line in p:lines() do
    if line:lower():match("%.lua$") then
      total = total + 1
      local f = line:gsub("\\", "/")
      local fn, err = loadfile(f)
      if not fn then
        failed = failed + 1
        if #failures < 20 then failures[#failures+1] = f .. " :: " .. tostring(err):gsub("\n"," ") end
      end
    end
  end
  p:close()
end
for _, r in ipairs(roots) do walk(r) end
print(("corpus: %d files, %d failed"):format(total, failed))
for _, f in ipairs(failures) do print("  FAIL " .. f) end
os.exit(failed == 0 and 0 or 1)
