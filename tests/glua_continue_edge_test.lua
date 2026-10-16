-- GLua continue edge cases: break+continue combos, closure capture,
-- repeat-until referencing body locals, nested loops, UCLO bookkeeping.

local fails = 0
local function ok( name, cond )
	if ( cond ) then
		print( "PASS  " .. name )
	else
		fails = fails + 1
		print( "FAIL  " .. name )
	end
end

-- 1) break AND continue in the same for-num loop
local s = 0
for i = 1, 10 do
	if ( i == 2 ) then continue end
	if ( i == 4 ) then break end
	s = s + i
end
ok( "for-num break+continue (1+3=4)", s == 4 )

-- 2) same in generic for
s = 0
for _, v in ipairs( { 1, 2, 3, 4, 5 } ) do
	if ( v == 2 ) then continue end
	if ( v == 4 ) then break end
	s = s + v
end
ok( "for-in break+continue (1+3=4)", s == 4 )

-- 3) same in while
s = 0
local n = 0
while ( n < 10 ) do
	n = n + 1
	if ( n == 2 ) then continue end
	if ( n == 4 ) then break end
	s = s + n
end
ok( "while break+continue (1+3=4)", s == 4 )

-- 4) same in repeat (continue re-evaluates the until condition)
s = 0
n = 0
repeat
	n = n + 1
	if ( n == 2 ) then continue end
	if ( n == 4 ) then break end
	s = s + n
until n >= 10
ok( "repeat break+continue (1+3=4)", s == 4 )

-- 5) closure capturing a block local, loop skipped by continue
--    (exercises UCLO close-level bookkeeping on the continue jump)
local fns = {}
for i = 1, 5 do
	local v = i * 10
	if ( i % 2 == 0 ) then continue end
	fns[ #fns + 1 ] = function() return v end
end
local total = 0
for _, f in ipairs( fns ) do total = total + f() end
ok( "closure capture with continue (10+30+50=90)", total == 90 and #fns == 3 )

-- 6) repeat whose until condition reads a body local, with continue
--    (continue must land before the condition, registers still live)
local i = 0
local seen = 0
repeat
	i = i + 1
	local done = ( i >= 4 )
	if ( i == 2 ) then continue end
	seen = seen + 1
until done
ok( "repeat until reads body local (iter 1,3,4 seen=3)", seen == 3 and i == 4 )

-- 7) closure + continue inside repeat (upvalue close before re-loop)
local rs = {}
i = 0
repeat
	i = i + 1
	local x = i
	if ( i == 2 ) then continue end
	rs[ #rs + 1 ] = function() return x end
until i >= 4
total = 0
for _, f in ipairs( rs ) do total = total + f() end
ok( "repeat closure with continue (1+3+4=8)", total == 8 and #rs == 3 )

-- 8) continue only unwinds the inner loop; outer keeps counting
local outerCount = 0
for a = 1, 3 do
	outerCount = outerCount + 1
	for b = 1, 3 do
		if ( b == 1 ) then continue end
		if ( b == 3 ) then break end
	end
end
ok( "nested continue/break (outer 3 iters)", outerCount == 3 )

-- 9) continue in for-in over pairs with modification-safe iteration
local keys = 0
for k in pairs( { a = 1, b = 2, c = 3 } ) do
	if ( k == "b" ) then continue end
	keys = keys + 1
end
ok( "pairs continue (2 keys)", keys == 2 )

-- 10) goto and continue coexist in one loop
s = 0
for i = 1, 5 do
	if ( i == 2 ) then goto skip end
	if ( i == 3 ) then continue end
	s = s + i
	::skip::
end
ok( "goto+continue coexist (1+4+5=10)", s == 10 )

-- 11) bare continue as the last statement of an if-block (GMod idiom)
s = 0
for i = 1, 4 do
	if ( i == 3 ) then
		continue
	end
	s = s + i
end
ok( "bare continue in if block (1+2+4=7)", s == 7 )

-- 12) table.insert middle-insert return value in a hot loop (JIT nyi path
--     falls back to the interpreter; both must agree)
local acc = 0
for j = 1, 2000 do
	local t = { 1, 2, 3 }
	acc = acc + ( table.insert( t, 2, 99 ) or -1 )
end
ok( "middle-insert under JIT (want 4000)", acc == 4000 )

print( fails == 0 and "ALL CLEAN" or ( fails .. " FAILED" ) )
os.exit( fails == 0 and 0 or 1 )
