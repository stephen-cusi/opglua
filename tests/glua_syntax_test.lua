-- GLua syntax/behaviour smoke test for the rewritten VM.
-- Run: luajit.exe glua_syntax_test.lua
-- Every check prints PASS/FAIL; the script exits non-zero on any failure.

local fails = 0
local function ok( name, cond )
	if ( cond ) then
		print( "PASS  " .. name )
	else
		fails = fails + 1
		print( "FAIL  " .. name )
	end
end

-- 1) C-style boolean operators: || && ! !=
local a, b = true, false
ok( "|| operator", ( a || b ) == true )
ok( "&& operator", ( a && b ) == false )
ok( "! operator", ( !b ) == true )
ok( "!= operator", ( 1 != 2 ) == true )
ok( "mixed expr", ( !b || ( a && !b ) ) == true )

-- 2) // line comment
local x = 5 // this is a comment
ok( "// comment", x == 5 )

-- 3) continue in numeric for
local sum = 0
for i = 1, 10 do
	if ( i % 2 == 0 ) then continue end
	sum = sum + i
end
ok( "continue in numeric for (odd sum 1..10 = 25)", sum == 25 )

-- 4) continue in while
local n, wsum = 0, 0
while ( n < 10 ) do
	n = n + 1
	if ( n > 5 ) then continue end
	wsum = wsum + n
end
ok( "continue in while (1..5 = 15)", wsum == 15 )

-- 5) continue in repeat
local m, rsum = 0, 0
repeat
	m = m + 1
	if ( m == 3 ) then continue end
	rsum = rsum + m
until m >= 5
ok( "continue in repeat (1+2+4+5 = 12)", rsum == 12 )

-- 6) continue in generic for
local gsum = 0
for _, v in ipairs( { 1, 2, 3, 4, 5 } ) do
	if ( v == 3 ) then continue end
	gsum = gsum + v
end
ok( "continue in generic for (1+2+4+5 = 12)", gsum == 12 )

-- 7) continue only affects the innermost loop
local outer = 0
for i = 1, 3 do
	for j = 1, 3 do
		if ( j == 2 ) then continue end
		outer = outer + 1
	end
end
ok( "nested continue (3 outer x 2 inner = 6)", outer == 6 )

-- 8) continue closes upvalues like break (loop var capture must not leak)
local captured = 0
for i = 1, 3 do
	local c = i
	if ( c == 2 ) then continue end
	captured = captured + c
end
ok( "continue with local capture (1+3 = 4)", captured == 4 )

-- 9) break still works alongside continue
local bsum = 0
for i = 1, 100 do
	if ( i > 5 ) then break end
	bsum = bsum + i
end
ok( "break unaffected (1..5 = 15)", bsum == 15 )

-- 10) error when continue used outside a loop
local f = loadstring( "continue" )
ok( "continue outside loop raises a syntax error", f == nil )

-- 11) reserved word: 'continue' cannot be used as an identifier
local g = loadstring( "local continue = 1" )
ok( "continue is reserved (no identifier clash)", g == nil )


-- 12) a lone '!' parses as a unary not via the raw char token path
ok( "double ! (!!true)", ( !!true ) == true )
ok( "! with != inside", ( !( 1 != 1 ) ) == true )
ok( "unary ! precedence (!!0 is true, !0 is false)", ( !!0 ) == true and ( !0 ) == false )

-- 13) a lone '!' in a bad spot renders as near '!' (raw char token), not 'not'
local e1 = select( 2, loadstring( "local x = 1 ! 2" ) )
ok( "illegal ! renders near '!'", e1 ~= nil and e1:find( "near '!'", 1, true ) ~= nil )
ok( "illegal ! does not render as 'not'", e1 == nil or e1:find( "near 'not'", 1, true ) == nil )

-- 14) unclosed /* block comment runs to EOF silently: the parser only sees
--     <eof>.  (Only the --[[ long-comment path reports "unfinished long
--     comment".)  Reference behavior: a complete statement before the comment
--     still compiles; an EOF mid-expression errors "near '<eof>'".
ok( "unclosed /* after a full statement compiles",
	loadstring( "local x = 1 /* oops" ) ~= nil )
local e2 = select( 2, loadstring( "local x = 1 + /* oops" ) )
ok( "unclosed /* mid-expression is a syntax error", e2 ~= nil )
ok( "unclosed /* says <eof>, not unfinished",
	e2:find( "<eof>", 1, true ) ~= nil and e2:find( "unfinished", 1, true ) == nil )

-- 15) a closed /* */ block comment still works and does not nest
local y = 1 /* closed */ + 1
ok( "closed /* */ comment", y == 2 )
local e3 = select( 2, loadstring( "/* /* still one comment */ */" ) )
ok( "/* */ does not nest (trailing */ errors)", e3 ~= nil )

print( fails == 0 and "ALL CLEAN" or ( fails .. " FAILED" ) )
os.exit( fails == 0 and 0 or 1 )
